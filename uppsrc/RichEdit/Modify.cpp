#include "RichEdit.h"

NAMESPACE_UPP


void RichEdit::Filter(RichText& txt) {}

void BegSelFixRaw(RichText& text)
{
	RichPos p = text.GetRichPos(0, 1);
	ASSERT(p.table == 1);
	if(p.table != 1)
		return;
	RichPara::Format fmt;
	text.InsertParaSpecial(1, true, fmt);
}

void BegSelUnFixRaw(RichText& text)
{
	ASSERT(text.GetLength() > 0);
	RichPos p = text.GetRichPos(1, 1);
	ASSERT(p.table == 1);
	if(p.table != 1)
		return;
	text.RemoveParaSpecial(1, true);
}

void RichEdit::UndoBegSelFix::Apply(RichText& txt)
{
	BegSelUnFixRaw(txt);
}

RichEdit::UndoRec *RichEdit::UndoBegSelFix::GetRedo(const RichText& txt)
{
	return new RichEdit::UndoBegSelUnFix;
}

void RichEdit::UndoBegSelUnFix::Apply(RichText& text)
{
	BegSelFixRaw(text);
}

RichEdit::UndoRec * RichEdit::UndoBegSelUnFix::GetRedo(const RichText& txt)
{
	return new RichEdit::UndoBegSelFix;
}

bool RichEdit::BegSelTabFix()
{
	if(begtabsel) {
		int c = cursor;
		AddUndo(new UndoBegSelFix);
		BegSelFixRaw(text);
		Move(0);
		Move(c + 1, true);
		begtabsel = false;
		return true;
	}
	return false;
}

void RichEdit::BegSelTabFixEnd(bool fix)
{
	if(fix && GetLength() > 0) {
		int c = cursor;
		AddUndo(new UndoBegSelUnFix);
		BegSelUnFixRaw(text);
		Move(0);
		Move(c - 1, true);
		begtabsel = true;
	}
}

bool RichEdit::InvalidRange(int l, int h)
{
	return !InSameTxt(text.GetRichPos(min(l, h)), text.GetRichPos(max(l, h)));
}

void RichEdit::AddUndo(UndoRec *ur)
{
	redo.Clear();
	SetModify();
	modified = true;
	incundoserial = true;
	while(undo.GetCount() > undosteps)
		undo.DropHead();
	found = false;
	ur->cursor = cursor;
	ur->serial = undoserial;
	undo.AddTail(ur);
}

void RichEdit::SaveStylesUndo()
{
	AddUndo(new UndoStyles(text));
}

void RichEdit::SaveStyleUndo(const Uuid& id)
{
	AddUndo(new UndoStyle(text, id));
}

void RichEdit::SaveFormat(int pos, int count)
{
	Limit(pos, count);
	AddUndo(new UndoFormat(text, pos, count));
}

void RichEdit::SaveFormat()
{
	int pos, count;
	if(IsSelection()) {
		if(tablesel) {
			SaveTable(tablesel);
			return;
		}
		pos = min(cursor, anchor);
		count = abs(cursor - anchor);
	}
	else {
		pos = cursor;
		count = 0;
	}
	bool b = BegSelTabFix();
	SaveFormat(pos, count);
	BegSelTabFixEnd(b);
}

void RichEdit::Limit(int& pos, int& count)
{
	int h = pos + count;
	pos = min(GetLength(), pos);
	count = min(GetLength(), h) - pos;
}

void RichEdit::ModifyFormat(int pos, const RichText::FormatInfo& fi, int count)
{
	if(IsReadOnly())
		return;
	bool b = BegSelTabFix();
	Limit(pos, count);
	SaveFormat(pos, count);
	text.ApplyFormatInfo(pos, fi, count);
	BegSelTabFixEnd(b);
}

void RichEdit::Remove(int pos, int len, bool forward)
{
	if(IsReadOnly())
		return;
	Limit(pos, len);
	if(InvalidRange(pos, pos + len))
		return;
	RichTxt::FormatInfo fi;
	if(forward)
		fi = text.GetFormatInfo(pos, 0);
	AddUndo(new UndoRemove(text, pos, len));
	text.Remove(pos, len);
	if(forward) {
		SaveFormat(pos, 0);
		text.ReplaceStyle(pos, fi.styleid);
		fi.paravalid &= ~RichText::STYLE;
		text.ApplyFormatInfo(pos, fi, 0);
	}
	SetModify();
	modified = true;
}

void RichEdit::Insert(int pos, const RichText& txt, bool typing)
{
	if(IsReadOnly())
		return;
	Index<int> lng;
	for(int i = 0; i < language.GetCount(); i++)
		lng.Add(language.GetKey(i));
	Vector<int> lngn = txt.GetAllLanguages();
	for(int i = 0; i < lngn.GetCount(); i++)
		lng.FindAdd(lngn[i]);
	SetupLanguage(lng.PickKeys());
	int l = text.GetLength();
	text.Insert(pos, txt);
	l = text.GetLength() - l;
	SetModify();
	modified = true;
	if(undo.GetCount()) {
		UndoRec& u = undo.Tail();
		if(typing) {
			UndoInsert *ui = dynamic_cast<UndoInsert *>(&u);
			if(ui && ui->length > 0 && ui->typing && ui->pos + ui->length == pos) {
				ui->length += l;
				return;
			}
		}
	}
	AddUndo(new UndoInsert(pos, l, typing));
}

void RichEdit::Undo()
{
	if(IsReadOnly())
		return;
	if(undo.IsEmpty()) return;
	CancelSelection();
	int serial = undo.Tail().serial;
	int c = cursor;
	while(undo.GetCount()) {
		UndoRec& u = undo.Tail();
		if(u.serial != serial) break;
		UndoRec *r = u.GetRedo(text);
		r->serial = u.serial;
		r->cursor = cursor;
		redo.Add(r);
		u.Apply(text);
		c = u.cursor;
		undo.DropTail();
		modified = true;
	}
	ReadStyles();
	Move(c);	
}

void RichEdit::Redo()
{
	if(IsReadOnly())
		return;
	if(redo.IsEmpty()) return;
	NextUndo();
	CancelSelection();
	int serial = redo.Top().serial;
	int c = cursor;
	while(redo.GetCount()) {
		UndoRec& r = redo.Top();
		if(r.serial != serial) break;
		UndoRec *u = r.GetRedo(text);
		u->serial = r.serial;
		u->cursor = cursor;
		undo.AddTail(u);
		r.Apply(text);
		c = r.cursor;
		redo.Drop();
		modified = true;
	}
	ReadStyles();
	Move(c);
}

#ifdef PLATFORM_WIN32
#define RTFS "Rich Text Format"
#else
#define RTFS "text/richtext"
#endif

RichText RichEdit::GetSelection(int maxcount) const
{
	RichText clip;
	if(tablesel) {
		RichTable tab = text.CopyTable(tablesel, cells);
		clip.SetStyles(text.GetStyles());
		clip.CatPick(tab);
	}
	else {
		if(begtabsel) {
			RichPos p = text.GetRichPos(0, 1);
			if(p.table) {
				RichTable tab = text.CopyTable(p.table);
				clip.SetStyles(text.GetStyles());
				clip.CatPick(tab);
				clip.CatPick(text.Copy(p.tablen + 1, minmax(abs(cursor - p.tablen - 1), 0, maxcount)));
			}
		}
		else
			clip = text.Copy(min(cursor, anchor), min(maxcount, abs(cursor - anchor)));
	}
	return clip;
}

void RichEdit::Cut()
{
	if(IsReadOnly())
		return;
	Copy();
	if(IsSelection())
		RemoveSelection();
	else
	if(objectpos >= 0) {
		Remove(cursor, 1);
		Move(cursor, false);
	}
}

void RichEdit::PasteText(const RichText& text)
{
	SetModify();
	modified = true;
	RemoveSelection();
	Insert(cursor, text, false);
	ReadStyles();
	DDUMP(text.GetLength());
	DDUMP(cursor);
	Move(cursor + text.GetLength(), false);
	DDUMP(cursor);
}

struct ToParaIterator : RichText::Iterator {
	RichPara para;
	bool     space;

	virtual bool operator()(int pos, const RichPara& p) {
		for(int i = 0; i < p.GetCount(); i++) {
			const RichPara::Part& part = p[i];
			if(part.IsText()) {
				const wchar *s = part.text;
				while(*s) {
					while(*s && *s <= ' ') {
						space = true;
						s++;
					}
					const wchar *t = s;
					while(*s > ' ') s++;
					if(s > t) {
						if(space)
							para.Cat(" ", part.format);
						para.Cat(WString(t, s), part.format);
						space = false;
					}
				}
			}
			else if(!part.field.IsNull()) {
				para.Cat(part.field, part.fieldparam, part.format);
				space = false;
			}
			else if(part.object) {
				para.Cat(part.object, part.format);
				space = false;
			}
		}
		space = true;
		return false;
	}

	ToParaIterator() { space = false; }
};

void RichEdit::ToPara()
{
	if(IsReadOnly())
		return;
	if(!IsSelection() || tablesel)
		return;
	NextUndo();
	RichText txt = text.Copy(min(cursor, anchor), abs(cursor - anchor));
	ToParaIterator it;
	txt.Iterate(it);
	RichText h;
	h.SetStyles(txt.GetStyles());
	h.Cat(it.para);
	PasteText(h);
}

void RichEdit::RemoveText(int count)
{
	CancelSelection();
	Remove(cursor, count);
	Finish();
}

RichText RichEdit::CopyText(int pos, int count) const
{
	return text.Copy(pos, count);
}

void RichEdit::InsertObject(int type)
{
	RichObjectType& richtype = RichObject::GetType(type);
	RichObject object = RichObject(&richtype, Value());
	RichObject o = object;
	o.DefaultAction(context);
	if(o.GetSerialId() != object.GetSerialId()) {
		RichText::FormatInfo finfo = GetFormatInfo();
		RemoveSelection();
		RichPara p;
		p.Cat(o, finfo);
		RichText clip;
		clip.Cat(p);
		Insert(GetCursor(), clip, false);
		Finish();
	}
}

void RichEdit::ReplaceObject(const RichObject& obj)
{
	Remove(objectpos, 1);
	RichPara p;
	p.Cat(obj, formatinfo);
	RichText clip;
	clip.Cat(p);
	Insert(objectpos, clip, false);
	Finish();
	objectrect = GetObjectRect(objectpos);
}

RichObject RichEdit::GetObject() const
{
	return text.GetRichPos(objectpos).object;
}

void RichEdit::LoadImage()
{
	FileSel fsel;
	fsel.AllFilesType();
	if(fsel.ExecuteOpen(t_("Open image from file"))) {
		FileIn fi;
		if(!fi.Open(~fsel)) {
			Exclamation(NFormat(t_("Error opening file [* \1%s\1]."), ~fsel));
			return;
		}
		Image img = StreamRaster::LoadAny(fi);
		if(IsNull(img)) {
			Exclamation(NFormat(t_("Unsupported image format in file [* \1%s\1]."), ~fsel));
			return;
		}
		Size dots = img.GetDots();
		if(dots.cx <= 0 || dots.cy <= 0)
			dots = img.GetSize() * 6;
		RichObject object = CreatePNGObject(img, dots, Size(Null));
		RichText::FormatInfo finfo = GetFormatInfo();
		RemoveSelection();
		RichPara p;
		p.Cat(object, finfo);
		RichText clip;
		clip.Cat(p);
		Insert(GetCursor(), clip, false);
		Finish();
	}
}

void RichEdit::Select(int pos, int count)
{
	found = false;
	Move(pos);
	Move(pos + count, true);
}

void RichEdit::InsertLine()
{
	if(IsReadOnly())
		return;
	RichText::FormatInfo b = formatinfo;
	RichText h;
	h.SetStyles(text.GetStyles());
	RichPara p;
	p.format = formatinfo;
	h.Cat(p);
	h.Cat(p);
	bool st = cursorp.paralen == cursorp.posinpara;
	bool f = cursorp.posinpara == 0 && formatinfo.label.GetCount();
	Insert(cursor, h, false);
	if(f) {
		String lbl = formatinfo.label;
		formatinfo.label.Clear();
		ApplyFormat(0, RichText::LABEL);
		formatinfo.label = lbl;
	}
	anchor = cursor = cursor + 1;
	begtabsel = false;
	formatinfo.newpage = false;
	if(st) {
		Uuid next = text.GetStyle(b.styleid).next;
		if(next != formatinfo.styleid) {
			formatinfo.label.Clear();
			formatinfo.styleid = next;
			ApplyFormat(0, RichText::STYLE|RichText::NEWPAGE|RichText::LABEL);
			return;
		}
	}
	ApplyFormat(0, RichText::NEWPAGE|RichText::LABEL);
	objectpos = -1;
}

END_UPP_NAMESPACE
