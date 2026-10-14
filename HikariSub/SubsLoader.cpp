//  Copyright (c) 2012 - 2026, Marcin Drob
//  Copyright (c) 2026, altqx

//  HikariSub is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.

//  HikariSub is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
//  along with HikariSub.  If not, see <http://www.gnu.org/licenses/>.




#include "SubsLoader.h"
#include "SubsGrid.h"
#include "HikariMessageBox.h"
#include "config.h"
#include <wx/tokenzr.h>


SubsLoader::SubsLoader(SubsGrid *_grid, const wxString &text, wxString &ext)
{
	grid = _grid;
	//the grid hands over a new file and keeps its own state until this succeeds
	grid->originalFormat = 0;
	grid->hasTLMode = false;
	bool validFormat = false;
	if (ext == L"ass" || ext == L"ssa"){
		succeeded = LoadASS(text);
		if (!succeeded){
			succeeded = LoadSRT(text);
			if (!succeeded){
				succeeded = LoadTXT(text);
				if (succeeded) ext = L"txt";
			}
			else{ ext = L"srt"; }
		}
		else{ validFormat = true; }
	}
	else if (ext == L"srt"){
		succeeded = LoadSRT(text);
		if (!succeeded){
			succeeded = LoadASS(text);
			if (!succeeded){
				succeeded = LoadTXT(text);
				if (succeeded) ext = L"txt";
			}
			else{ ext = L"ass"; }
		}
		else{ validFormat = true; }
	}
	else{
		succeeded = LoadTXT(text);
		grid->SetSubsFormat();
		if (grid->subsFormat == SRT){
			grid->file->Clear();
			succeeded = LoadSRT(text);
			if (succeeded) ext = L"srt";
		}
		else if (grid->subsFormat == ASS){
			grid->file->Clear();
			succeeded = LoadASS(text);
			ext = L"ass";
			if (!succeeded){
				grid->LoadDefault(false, false, false);
				succeeded = LoadTXT(text);
				//told by the grid once the file is loaded, so no window is
				//painted with this file and the old file's state
				plainTextAsAss = true;
			}
		}
		else{ validFormat = true; }
	}
	//WARNING! table can not be empty
	//text helper class will crash when gets nullptr in = operator
	//on failure the grid puts its previous subtitles back
	if (succeeded){
		grid->SetSubsFormat();
		if (validFormat)
			grid->originalFormat = grid->subsFormat;
		// the attached fonts reach the renderers before anything is drawn,
		// the style previews included
		grid->file->Attachments();
	}
}

bool SubsLoader::LoadASS(const wxString &text)
{
	short section = 0;
	char format = ASS;
	wxStringTokenizer tokenizer(text, L"\n", wxTOKEN_STRTOK);

	bool tlmode = false;
	bool hasFiltering = false;
	wxString tlstyle;


	// embedded fonts and pictures are uuencoded lines, kept as they are,
	// and so are sections this loader does not know
	bool embedded = false;
	// lines before the first section header count as script info
	bool scriptInfo = true;
	// a line read after a translation original that is not its translation
	wxString nextToken;
	while (!nextToken.empty() || tokenizer.HasMoreTokens())
	{
		wxString token = (nextToken.empty()) ? tokenizer.GetNextToken().Trim(false) : nextToken;
		nextToken.clear();
		if (token.empty()){ continue; }
		if (token[0] == L'['){
			wxString header = token.Lower();
			scriptInfo = header.StartsWith(L"[script info]");
			embedded = !scriptInfo && !header.StartsWith(L"[v4") && !header.StartsWith(L"[events]");
		}
		if (embedded) {
			grid->file->AddEmbeddedSectionLine(token.Trim(true));
			continue;
		}
		if ((token.StartsWith(L"Dial") || token.StartsWith(L"Comm") || (token[0] == L';' && section > 2))){
			Dialogue *dl = new Dialogue(token);
			// put it on start cause in tlmode it releases dl
			if (!hasFiltering && dl->isVisible != VISIBLE && !dl->NonDialogue && !dl->treeState)
				hasFiltering = true;

			if (!tlmode){
				grid->file->AddLine(dl);
			}
			else if (tlmode && dl->Style == tlstyle){
				// the translation is the next dialogue or comment line in another style,
				// anything else is read on its own and the original stays alone
				wxString ntoken;
				while (ntoken.empty() && tokenizer.HasMoreTokens())
					ntoken = tokenizer.GetNextToken().Trim(false);
				Dialogue *tl = nullptr;
				if (ntoken.StartsWith(L"Dial") || ntoken.StartsWith(L"Comm")){
					tl = new Dialogue(ntoken);
					if (tl->Format != ASS || tl->NonDialogue || tl->Style == tlstyle){
						delete tl;
						tl = nullptr;
					}
				}
				if (!tl){
					nextToken = ntoken;
					grid->file->AddLine(dl);
					continue;
				}
				tl->TextTl = tl->Text;
				tl->Text = dl->Text;
				tl->tlPair = true;
				if (dl->Effect == L"\fD"){
					tl->ChangeState(4);
				}
				grid->file->AddLine(tl);
				delete dl;
			}
			else{
				grid->file->AddLine(dl);
			}
		}
		else if (token.StartsWith(L"Style:"))
		{
			//1 = ASS, 2 = SSA, needs only for subtitles loading.
			grid->file->AddStyle(new Styles(token, format));
			section = 2;
		}
		else if (token.StartsWith(L"[V4")){
			if (!token.StartsWith(L"[V4+")){
				format = 2;
				grid->file->AddSInfo(L"ScriptType", L"4.00+");
			}
			//format SSA was removed from working formats
			//need to send to constructor of styles 2 as format SSA, SRT has 3 to avoid bugs
			section = 1;
		}
		else if (scriptInfo && token[0] != L';' && token[0] != L'[' && token.Find(L':') != wxNOT_FOUND && !token.StartsWith(L"Format")){
			grid->file->AddSInfo(token);
		}
		else if (token.StartsWith(L"[Eve")){
			tlmode = (grid->file->GetSInfo(L"TLMode") == L"Yes");
			if (tlmode){ tlstyle = grid->file->GetSInfo(L"TLMode Style"); if (tlstyle == emptyString){ tlmode = false; } }
			section = 3;
		}
	}
	grid->hasTLMode = tlmode;
	grid->file->SetFiltered(hasFiltering);
	return grid->file->GetCount() > 0;
}

bool SubsLoader::LoadSRT(const wxString &srttext)
{
	wxStringTokenizer tokenizer(srttext, L"\n", wxTOKEN_STRTOK);
	wxString text1;
	
	while (tokenizer.HasMoreTokens()){
		wxString text = tokenizer.GetNextToken().Trim();
		//a CRLF blank line is a lone \r token, skip it like an LF blank line
		if (text.empty()){ continue; }
		//seek for every times line
		if (text.find(L" --> ") != -1){
			//trim number of next line 
			TrimLastNumber(&text1);
			//first line is always empty after "1" trim
			//it's not if there is no number
			if (!text1.empty()){
				grid->file->AddLine(new Dialogue(text1));
				text1.Empty();
			}
			//set next line times
			text1 << text << L"\r\n";
		}
		else{ 
			text1 << text << L"\r\n"; 
		}
		
	}

	if (text1 != emptyString){
		grid->file->AddLine(new Dialogue(text1.Trim()));
		text1 = emptyString;
	}
	return grid->file->GetCount() > 0;
}

bool SubsLoader::LoadTXT(const wxString &text)
{
	wxStringTokenizer tokenizer(text, L"\n", wxTOKEN_STRTOK);
	while (tokenizer.HasMoreTokens()){
		wxString text = tokenizer.GetNextToken().Trim();
		grid->file->AddLine(new Dialogue(text.Trim()));
	}
	return grid->file->GetCount() > 0;
}

void SubsLoader::TrimLastNumber(wxString* text)
{
	//if size is less than 3 it means there is only \r\n
	if (text->length() < 3) {
		text->Empty();
		return;
	}

	wxString digits = L"0123456789\r\n";
	bool foundR = false;
	//skip first \r\n
	for (size_t i = text->length() - 2; i > 0; i--) {
		wxUniChar ch = (*text)[i - 1];
		size_t digitPos = digits.find(ch);
		if (digitPos == -1) {
			//remove digits and \r \n
			text->RemoveLast(text->length() - i);
			break;
		}//ensure to remove only first number \r\nnumber\r\n
		else if (digitPos == 10) {
			//remove digits and \r \n
			text->RemoveLast(text->length() - (i - 1));
			break;
		}
		else if (i == 1)
			text->Empty();
	}
}

