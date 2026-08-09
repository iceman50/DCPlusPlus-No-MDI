/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "RichTextEditorDlg.h"

#include <dcpp/File.h>
#include <dcpp/RichText.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/ShareManager.h>

#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/LoadDialog.h>
#include <dwt/widgets/MessageBox.h>

#include "HtmlToRtf.h"
#include "RichTextBox.h"
#include "WinUtil.h"

using dwt::Button;
using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using dwt::LoadDialog;

namespace {

tstring prefixLines(const tstring& text, const tstring& prefix, bool numbered = false) {
	tstring result;
	result.reserve(text.size() + prefix.size() * 2);
	size_t line = 1;
	result += numbered ? Text::toT(std::to_string(line++) + ". ") : prefix;
	for(size_t i = 0; i < text.size(); ++i) {
		result += text[i];
		if(text[i] == _T('\n') && i + 1 < text.size()) {
			result += numbered ? Text::toT(std::to_string(line++) + ". ") : prefix;
		}
	}
	return result;
}

void normalizeAdcLineEndings(string& text) {
	for(size_t i = 0; i < text.size(); ++i) {
		if(text[i] != '\r') continue;
		if(i + 1 < text.size() && text[i + 1] == '\n') text.erase(i, 1);
		else text[i] = '\n';
	}
}

}

RichTextEditorDlg::RichTextEditorDlg(dwt::Widget* parent, const string& aHubUrl, const tstring& aInitialText) :
	GridDialog(parent, 780),
	hubUrl(aHubUrl),
	initialText(aInitialText),
	source(nullptr),
	preview(nullptr),
	validation(nullptr),
	useButton(nullptr),
	ready(false)
{
	onInitDialog([this] { return handleInitDialog(); });
}

bool RichTextEditorDlg::handleInitDialog() {
	WinUtil::setColor(this);

	grid = addChild(Grid::Seed(7, 1));
	grid->column(0).mode = GridInfo::FILL;
	grid->setSpacing(6);
	WinUtil::setColor(grid);

	auto sourceLabel = grid->addChild(Label::Seed(T_("Markdown source")));
	WinUtil::setColor(sourceLabel);
	grid->setWidget(sourceLabel, 0, 0);

	TextBox::Seed sourceSeed = WinUtil::Seeds::Dialog::textBox;
	sourceSeed.style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
	source = grid->addChild(sourceSeed);
	WinUtil::setColor(source);
	grid->setWidget(source, 1, 0);
	grid->row(1).mode = GridInfo::STATIC;
	grid->row(1).size = 165;
	grid->row(1).align = GridInfo::STRETCH;
	source->setTextLimit(std::max(1024, SETTING(RICH_TEXT_MAX_SIZE)));

	auto builder = grid->addChild(Grid::Seed(2, 7));
	WinUtil::setColor(builder);
	grid->setWidget(builder, 2, 0);
	for(size_t i = 0; i < 7; ++i) builder->column(i).mode = GridInfo::FILL;

	auto addBuilderButton = [builder](const tstring& caption, const std::function<void()>& action) {
		auto button = builder->addChild(Button::Seed(caption));
		button->onClicked(action);
		return button;
	};

	addBuilderButton(T_("Bold"), [this] { wrapSelection(_T("**"), _T("**"), T_("bold text")); });
	addBuilderButton(T_("Italic"), [this] { wrapSelection(_T("_"), _T("_"), T_("italic text")); });
	addBuilderButton(T_("Strike"), [this] { wrapSelection(_T("~~"), _T("~~"), T_("removed text")); });
	addBuilderButton(T_("Heading"), [this] {
		auto selected = source->getSelection();
		if(selected.empty()) selected = T_("Heading");
		insertBlock(prefixLines(selected, _T("## ")));
	});
	addBuilderButton(T_("Link"), [this] { wrapSelection(_T("["), _T("](https://example.org/)"), T_("link text")); });
	addBuilderButton(T_("Quote"), [this] {
		auto selected = source->getSelection();
		if(selected.empty()) selected = T_("Quoted text");
		insertBlock(prefixLines(selected, _T("> ")));
	});
	addBuilderButton(T_("Inline code"), [this] { wrapSelection(_T("`"), _T("`"), T_("code")); });

	addBuilderButton(T_("Bullets"), [this] {
		auto selected = source->getSelection();
		if(selected.empty()) selected = T_("First item") + _T("\r\n") + T_("Second item");
		insertBlock(prefixLines(selected, _T("- ")));
	});
	addBuilderButton(T_("Numbered"), [this] {
		auto selected = source->getSelection();
		if(selected.empty()) selected = T_("First item") + _T("\r\n") + T_("Second item");
		insertBlock(prefixLines(selected, _T(""), true));
	});
	addBuilderButton(T_("Code block"), [this] {
		auto selected = source->getSelection();
		if(selected.empty()) selected = T_("code");
		insertBlock(_T("```\r\n") + selected + _T("\r\n```"));
	});
	addBuilderButton(T_("Table"), [this] {
		insertBlock(_T("| ") + T_("Left") + _T(" | ") + T_("Center") + _T(" | ") + T_("Right") +
			_T(" |\r\n| :--- | :---: | ---: |\r\n| A | B | C |"));
	});
	addBuilderButton(T_("Rule"), [this] { insertBlock(_T("---")); });
	addBuilderButton(T_("Attach file..."), [this] { insertAttachment(false); });
	addBuilderButton(T_("Inline media..."), [this] { insertAttachment(true); });

	auto previewLabel = grid->addChild(Label::Seed(T_("Live preview")));
	WinUtil::setColor(previewLabel);
	grid->setWidget(previewLabel, 3, 0);

	RichTextBox::Seed previewSeed = WinUtil::Seeds::richTextBox;
	previewSeed.style |= ES_READONLY | WS_VSCROLL;
	preview = dwt::WidgetCreator<RichTextBox>::create(grid, previewSeed);
	WinUtil::setColor(preview);
	const auto richTextLimit = std::max(1024, SETTING(RICH_TEXT_MAX_SIZE));
	preview->setTextLimit(richTextLimit > std::numeric_limits<int>::max() / 4 ?
		std::numeric_limits<int>::max() : richTextLimit * 4);
	grid->setWidget(preview, 4, 0);
	grid->row(4).mode = GridInfo::STATIC;
	grid->row(4).size = 165;
	grid->row(4).align = GridInfo::STRETCH;

	validation = grid->addChild(Label::Seed());
	WinUtil::setColor(validation);
	grid->setWidget(validation, 5, 0);

	auto buttons = grid->addChild(Grid::Seed(1, 2));
	WinUtil::setColor(buttons);
	grid->setWidget(buttons, 6, 0);
	auto dialogButtons = WinUtil::addDlgButtons(buttons,
		[this] { handleUse(); },
		[this] { endDialog(IDCANCEL); });
	useButton = dialogButtons.first;
	useButton->setText(T_("Use in chat"));
	useButton->setEnabled(false);

	source->onUpdated([this] { updatePreview(); });
	source->setText(initialText);
	source->setFocus();
	source->setSelection(static_cast<int>(source->length()), static_cast<int>(source->length()));

	setText(T_("RTF0 Markdown editor"));
	layout();
	centerWindow();
	updatePreview();
	return false;
}

void RichTextEditorDlg::updatePreview() {
	auto text = Text::fromT(source->getText());
	normalizeAdcLineEndings(text);
	const auto maxSize = static_cast<size_t>(std::max(1024, SETTING(RICH_TEXT_MAX_SIZE)));
	const auto maxTarget = static_cast<size_t>(std::max(1, SETTING(CHAT_LINK_MAX_LENGTH)));

	ready = false;
	preview->setText(Util::emptyStringT);

	if(text.empty()) {
		validation->setText(T_("Start typing Markdown or use the builder buttons."));
		useButton->setEnabled(false);
		return;
	}
	if(text.size() > maxSize) {
		validation->setText(str(TF_("Message is too large: %1% of %2% bytes.") % text.size() % maxSize));
		useButton->setEnabled(false);
		return;
	}

	const auto parsed = RichText::parse(text, maxTarget);
	if(parsed.valid) {
		const auto rtf = _T("{\\urtf1\n") + HtmlToRtf::convert(parsed.html, preview) + _T("}\n");
		preview->addTextSteady(rtf);
		preview->setSelection(0, 0);
		preview->sendMessage(WM_VSCROLL, SB_TOP);
	}

	if(!parsed.valid) {
		validation->setText(T_("Markdown exceeds the supported nesting depth or could not be parsed."));
	} else if(!parsed.safeToSend) {
		validation->setText(T_("Inline media must use a valid magnet containing a TTH and exact file size."));
	} else if(!parsed.formatted) {
		validation->setText(T_("Add at least one Markdown formatting construct before sending with RTF0."));
	} else {
		auto prepared = text;
		if(!RichText::prepareOutgoingMessage(prepared, true, hubUrl)) {
			validation->setText(T_("An attachment is unavailable in this hub's share or its declared size does not match."));
		} else {
			ready = true;
			validation->setText(str(TF_("Ready: %1% bytes, %2% attachment(s).") % text.size() % parsed.attachments.size()));
		}
	}
	useButton->setEnabled(ready);
}

void RichTextEditorDlg::handleUse() {
	if(!ready) return;
	result = source->getText();
	endDialog(IDOK);
}

void RichTextEditorDlg::wrapSelection(const tstring& before, const tstring& after, const tstring& placeholder) {
	const auto range = source->getCaretPosRange();
	auto selected = source->getSelection();
	const auto text = source->getText();

	if(!selected.empty() && !before.empty() && !after.empty()) {
		// Toggle when the delimiters themselves are part of the selection.
		if(selected.size() >= before.size() + after.size() &&
			selected.compare(0, before.size(), before) == 0 &&
			selected.compare(selected.size() - after.size(), after.size(), after) == 0)
		{
			auto content = selected.substr(before.size(), selected.size() - before.size() - after.size());
			source->replaceSelection(content);
			source->setSelection(range.first, range.first + static_cast<int>(content.size()));
			source->setFocus();
			return;
		}

		// The builder selects placeholder content between its delimiters. A second click should remove
		// that surrounding pair rather than producing four tildes/asterisks/backticks on each side.
		const auto wrappedBegin = range.first - static_cast<int>(before.size());
		const auto wrappedEnd = range.second + static_cast<int>(after.size());
		if(wrappedBegin >= 0 && wrappedEnd <= static_cast<int>(text.size()) &&
			text.compare(static_cast<size_t>(wrappedBegin), before.size(), before) == 0 &&
			text.compare(static_cast<size_t>(range.second), after.size(), after) == 0)
		{
			source->setSelection(wrappedBegin, wrappedEnd);
			source->replaceSelection(selected);
			source->setSelection(wrappedBegin, wrappedBegin + static_cast<int>(selected.size()));
			source->setFocus();
			return;
		}
	}

	const auto usedPlaceholder = selected.empty();
	if(usedPlaceholder) selected = placeholder;

	source->replaceSelection(before + selected + after);
	const auto contentBegin = range.first + static_cast<int>(before.size());
	if(usedPlaceholder) {
		source->setSelection(contentBegin, contentBegin + static_cast<int>(selected.size()));
	} else {
		const auto end = range.first + static_cast<int>(before.size() + selected.size() + after.size());
		source->setSelection(end, end);
	}
	source->setFocus();
}

void RichTextEditorDlg::insertBlock(const tstring& block) {
	const auto range = source->getCaretPosRange();
	const auto text = source->getText();
	const auto needsLeadingBreak = range.first > 0 && text[range.first - 1] != _T('\n') && text[range.first - 1] != _T('\r');
	const auto needsTrailingBreak = range.second < static_cast<int>(text.size()) &&
		text[range.second] != _T('\n') && text[range.second] != _T('\r');
	const tstring inserted = (needsLeadingBreak ? _T("\r\n\r\n") : _T("")) + block +
		(needsTrailingBreak ? _T("\r\n\r\n") : _T(""));
	source->replaceSelection(inserted);
	const auto end = range.first + static_cast<int>(inserted.size());
	source->setSelection(end, end);
	source->setFocus();
}

void RichTextEditorDlg::insertAttachment(bool inlineMedia) {
	tstring file;
	if(!LoadDialog(this).addFilter(T_("All files"), _T("*.*")).open(file)) return;

	const auto realPath = Text::fromT(file);
	const auto tth = ShareManager::getInstance()->getTTHFromReal(realPath);
	if(!tth) {
		showAttachmentError(T_("The selected file is not currently shared or has not finished hashing."));
		return;
	}

	const auto size = File::getSize(realPath);
	if(size < 0) {
		showAttachmentError(T_("The selected file could not be read."));
		return;
	}

	try {
		const auto tthPath = "TTH/" + tth->toBase32();
		const auto shared = hubUrl.empty() ? ShareManager::getInstance()->toRealWithSize(tthPath) :
			ShareManager::getInstance()->toRealWithSize(tthPath, hubUrl);
		if(shared.second != size) {
			showAttachmentError(T_("The selected file's shared size does not match its current size."));
			return;
		}
	} catch(const Exception&) {
		showAttachmentError(T_("The selected file is not exposed by the share configured for this hub."));
		return;
	}

	const auto markdown = RichText::makeAttachmentMarkdown(
		Util::getFileName(realPath), tth->toBase32(), size, inlineMedia);
	source->replaceSelection(Text::toT(markdown));
	const auto end = source->getCaretPos();
	source->setSelection(end, end);
	source->setFocus();
}

void RichTextEditorDlg::showAttachmentError(const tstring& message) {
	dwt::MessageBox(this).show(message, T_("RTF0 attachment"),
		dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONEXCLAMATION);
}
