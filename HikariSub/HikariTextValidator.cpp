/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/valtext.cpp
// Purpose:     HikariTextValidator
// Author:      Julian Smart
// Modified by: Francesco Montorsi
// Created:     04/01/98
// RCS-ID:      $Id$
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".



#include "HikariTextValidator.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <wx/window.h>
#include "HikariTextCtrl.h"
#include "HikariMessageBox.h"

// ----------------------------------------------------------------------------
// global helpers
// ----------------------------------------------------------------------------

static bool wxIsNumeric(const wxString& val)
{
	for ( wxString::const_iterator i = val.begin(); i != val.end(); ++i )
	{
		// Allow for "," (French) as well as "." -- in future we should
		// use wxSystemSettings or other to do better localisation
		if ((!wxIsdigit(*i)) &&
			(*i != wxS('.')) && (*i != wxS(',')) && (*i != wxS('e')) &&
			(*i != wxS('E')) && (*i != wxS('+')) && (*i != wxS('-')))
			return false;
	}
	return true;
}

// ----------------------------------------------------------------------------
// HikariTextValidator
// ----------------------------------------------------------------------------

IMPLEMENT_DYNAMIC_CLASS(HikariTextValidator, wxValidator)
BEGIN_EVENT_TABLE(HikariTextValidator, wxValidator)
	EVT_CHAR(HikariTextValidator::OnChar)
END_EVENT_TABLE()

HikariTextValidator::HikariTextValidator(long style, wxString *val)
{
	m_stringValue = val;
	SetStyle(style);
}

HikariTextValidator::HikariTextValidator(const HikariTextValidator& val)
	: wxValidator()
{
	Copy(val);
}

void HikariTextValidator::SetStyle(long style)
{
	m_validatorStyle = style;

#if wxDEBUG_LEVEL
	int check;
	check = (int)HasFlag(wxFILTER_ALPHA) + (int)HasFlag(wxFILTER_ALPHANUMERIC) +
			(int)HasFlag(wxFILTER_DIGITS) + (int)HasFlag(wxFILTER_NUMERIC);
	/*wxASSERT_MSG(check <= 1,
		"It makes sense to use only one of the wxFILTER_ALPHA/wxFILTER_ALPHANUMERIC/"
		"wxFILTER_SIMPLE_NUMBER/wxFILTER_NUMERIC styles");*/

	/*wxASSERT_MSG(((int)HasFlag(wxFILTER_INCLUDE_LIST) + (int)HasFlag(wxFILTER_INCLUDE_CHAR_LIST) <= 1) &&
				 ((int)HasFlag(wxFILTER_EXCLUDE_LIST) + (int)HasFlag(wxFILTER_EXCLUDE_CHAR_LIST) <= 1),
		"Using both wxFILTER_[IN|EX]CLUDE_LIST _and_ wxFILTER_[IN|EX]CLUDE_CHAR_LIST "
		"doesn't work since HikariTextValidator internally uses the same array for both");*/

	check = (int)HasFlag(wxFILTER_INCLUDE_LIST) + (int)HasFlag(wxFILTER_INCLUDE_CHAR_LIST) +
			(int)HasFlag(wxFILTER_EXCLUDE_LIST) + (int)HasFlag(wxFILTER_EXCLUDE_CHAR_LIST);
	/*wxASSERT_MSG(check <= 1,
		"Using both an include/exclude list may lead to unexpected results");*/
#endif // wxDEBUG_LEVEL
}

bool HikariTextValidator::Copy(const HikariTextValidator& val)
{
	wxValidator::Copy(val);

	m_validatorStyle = val.m_validatorStyle;
	m_stringValue = val.m_stringValue;

	m_includes    = val.m_includes;
	m_excludes    = val.m_excludes;

	return true;
}

bool HikariTextValidator::Validate(wxWindow *parent)
{
	// If window is disabled, simply return
	if ( !m_validatorWindow->IsEnabled() )
		return true;

	HikariTextCtrl * const text = GetHikariTextCtrl();
	if ( !text )
		return false;

	wxString val(text->GetValue());

	wxString errormsg;
	if ( HasFlag(wxFILTER_EMPTY) && val.empty() )
	{
		errormsg = L"Required information entry is empty.";
	}
	else if ( !(errormsg = IsValid(val)).empty() )
	{
		// NB: this format string should always contain exactly one '%s'
		wxString buf;
		buf.Printf(errormsg, val.c_str());
		errormsg = buf;
	}

	if ( !errormsg.empty() )
	{
		m_validatorWindow->SetFocus();
		HikariMessageBox(errormsg, L"Text check Error",
					 wxOK | wxICON_EXCLAMATION, parent);

		return false;
	}

	return true;
}
	
bool HikariTextValidator::TransferToWindow()
{
	 if ( m_stringValue )
	{
		HikariTextCtrl * const text = GetHikariTextCtrl();
		if ( !text )
			return false;

		text->SetValue(*m_stringValue);
	}

	return true;
}
	
bool HikariTextValidator::TransferFromWindow()
{
	if ( m_stringValue )
	{
		HikariTextCtrl * const text = GetHikariTextCtrl();
		if ( !text )
			return false;

		*m_stringValue = text->GetValue();
	}

	return true;
}

HikariTextCtrl *HikariTextValidator::GetHikariTextCtrl()
{
	if (m_validatorWindow->IsKindOf(CLASSINFO(HikariTextCtrl)))
	{
		return (HikariTextCtrl*)m_validatorWindow;
	}

	/*wxFAIL_MSG(
		"HikariTextValidator can only be used with HikariTextCtrl"*/
	//);

	return nullptr;
}

// IRIX mipsPro refuses to compile wxStringCheck<func>() if func is inline so
// let's work around this by using this non-template function instead of
// wxStringCheck(). And while this might be fractionally less efficient because
// the function call won't be inlined like this, we don't care enough about
// this to add extra #ifs for non-IRIX case.
namespace
{

bool CheckString(bool (*func)(const wxUniChar&), const wxString& str)
{
	for ( wxString::const_iterator i = str.begin(); i != str.end(); ++i )
	{
		if ( !func(*i) )
			return false;
	}

	return true;
}

} // anonymous namespace

wxString HikariTextValidator::IsValid(const wxString& val) const
{
	// wxFILTER_EMPTY is checked for in HikariTextValidator::Validate

	if ( HasFlag(wxFILTER_ASCII) && !val.IsAscii() )
		return L"'%s' should only contain ASCII characters.";
	if ( HasFlag(wxFILTER_ALPHA) && !CheckString(wxIsalpha, val) )
		return L"'%s' should only contain alphabetic characters.";
	if ( HasFlag(wxFILTER_ALPHANUMERIC) && !CheckString(wxIsalnum, val) )
		return L"'%s' should only contain alphabetic or numeric characters.";
	if ( HasFlag(wxFILTER_DIGITS) && !CheckString(wxIsdigit, val) )
		return L"'%s' should only contain digits.";
	if ( HasFlag(wxFILTER_NUMERIC) && !wxIsNumeric(val) )
		return L"'%s' should be numeric.";
	if ( HasFlag(wxFILTER_INCLUDE_LIST) && m_includes.Index(val) == wxNOT_FOUND )
		return L"'%s' is invalid";
	if ( HasFlag(wxFILTER_INCLUDE_CHAR_LIST) && !ContainsOnlyIncludedCharacters(val) )
		return L"'%s' is invalid";
	if ( HasFlag(wxFILTER_EXCLUDE_LIST) && m_excludes.Index(val) != wxNOT_FOUND )
		return L"'%s' is invalid";
	if ( HasFlag(wxFILTER_EXCLUDE_CHAR_LIST) && ContainsExcludedCharacters(val) )
		return L"'%s' is invalid";

	return wxEmptyString;
}

bool HikariTextValidator::ContainsOnlyIncludedCharacters(const wxString& val) const
{
	for ( wxString::const_iterator i = val.begin(); i != val.end(); ++i )
		if (m_includes.Index((wxString) *i) == wxNOT_FOUND)
			// one character of 'val' is NOT present in m_includes...
			return false;

	// all characters of 'val' are present in m_includes
	return true;
}

bool HikariTextValidator::ContainsExcludedCharacters(const wxString& val) const
{
	for ( wxString::const_iterator i = val.begin(); i != val.end(); ++i )
		if (m_excludes.Index((wxString) *i) != wxNOT_FOUND)
			// one character of 'val' is present in m_excludes...
			return true;

	// all characters of 'val' are NOT present in m_excludes
	return false;
}

void HikariTextValidator::SetCharIncludes(const wxString& chars)
{
	wxArrayString arr;

	for ( wxString::const_iterator i = chars.begin(); i != chars.end(); ++i )
		arr.Add(*i);

	SetIncludes(arr);
}

void HikariTextValidator::SetCharExcludes(const wxString& chars)
{
	wxArrayString arr;

	for ( wxString::const_iterator i = chars.begin(); i != chars.end(); ++i )
		arr.Add(*i);

	SetExcludes(arr);
}

void HikariTextValidator::OnChar(wxKeyEvent& event)
{
	if (!m_validatorWindow)
	{
		event.Skip();
		return;
	}

	int keyCode = event.GetKeyCode();

	// we don't filter special keys and delete
	if (keyCode < WXK_SPACE || keyCode == WXK_DELETE || keyCode >= WXK_START)
	{
		event.Skip();
		return;
	}

	wxString str((wxUniChar)keyCode, 1);
	if (!IsValid(str).empty())
	{
		if ( !wxValidator::IsSilent() )
			wxBell();

		// eat message
		return;
	}
	else
		event.Skip();
}


 
