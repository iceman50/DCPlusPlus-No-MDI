/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"

#include "BBSPostDlg.h"

#include <dcpp/BBSManager.h>
#include <dcpp/format.h>
#include <dcpp/RichText.h>

#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/TextBox.h>

#include "RichTextEditorDlg.h"
#include "WinUtil.h"
#include "resource.h"

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using dwt::TextBox;

BBSPostDlg::BBSPostDlg(dwt::Widget* parent, const string& aHubUrl, const string& aBoard, const string& aParentTTH, const tstring& aInitialSubject, int64_t aMaxSize) :
	GridDialog(parent, 660),
	hubUrl(aHubUrl),
	board(aBoard),
	parentTTH(aParentTTH),
	initialSubject(aInitialSubject),
	maxSize(aMaxSize),
	subject(nullptr),
	body(nullptr),
	richText(nullptr),
	editRichText(nullptr),
	validation(nullptr),
	postButton(nullptr),
	ready(false),
	resultRichText(false)
{
	onInitDialog([this] { return handleInitDialog(); });
}

bool BBSPostDlg::handleInitDialog() {
	grid = addChild(Grid::Seed(7, 1));
	grid->column(0).mode = GridInfo::FILL;
	grid->setSpacing(6);

	auto boardRow = grid->addChild(Grid::Seed(1, 2));
	boardRow->column(0).align = GridInfo::BOTTOM_RIGHT;
	boardRow->column(1).mode = GridInfo::FILL;
	boardRow->setSpacing(grid->getSpacing());
	grid->setWidget(boardRow, 0, 0);
	boardRow->addChild(Label::Seed(T_("Board:")));
	boardRow->addChild(Label::Seed(Text::toT(BBSManager::sanitizeDisplayText(board, 128))));

	auto subjectRow = grid->addChild(Grid::Seed(1, 2));
	subjectRow->column(0).align = GridInfo::BOTTOM_RIGHT;
	subjectRow->column(1).mode = GridInfo::FILL;
	subjectRow->setSpacing(grid->getSpacing());
	grid->setWidget(subjectRow, 1, 0);
	subjectRow->addChild(Label::Seed(T_("Subject:")));
	subject = subjectRow->addChild(WinUtil::Seeds::Dialog::textBox);
	subject->setTextLimit(4096);

	auto bodyLabel = grid->addChild(Label::Seed(T_("Body")));
	grid->setWidget(bodyLabel, 2, 0);

	TextBox::Seed bodySeed = WinUtil::Seeds::Dialog::textBox;
	bodySeed.style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
	body = grid->addChild(bodySeed);
	const auto documentLimit = static_cast<int>(std::min<int64_t>(BBSManager::MAX_DOCUMENT_SIZE, std::numeric_limits<int>::max()));
	body->setTextLimit(documentLimit);
	grid->setWidget(body, 3, 0);
	grid->row(3).mode = GridInfo::STATIC;
	grid->row(3).size = 260;
	grid->row(3).align = GridInfo::STRETCH;

	auto options = grid->addChild(Grid::Seed(1, 3));
	options->column(1).mode = GridInfo::FILL;
	options->setSpacing(grid->getSpacing());
	grid->setWidget(options, 4, 0);
	richText = options->addChild(CheckBox::Seed(T_("RTF0 Markdown")));
	options->setWidget(richText, 0, 0);
	editRichText = options->addChild(Button::Seed(T_("Edit rich text...")));
	editRichText->setImage(WinUtil::buttonIcon(IDI_CHAT));
	options->setWidget(editRichText, 0, 2);

	validation = grid->addChild(Label::Seed());
	grid->setWidget(validation, 5, 0);

	auto buttons = grid->addChild(Grid::Seed(1, 2));
	buttons->column(0).mode = GridInfo::FILL;
	buttons->column(0).align = GridInfo::BOTTOM_RIGHT;
	buttons->setSpacing(grid->getSpacing());
	grid->setWidget(buttons, 6, 0);
	auto dialogButtons = WinUtil::addDlgButtons(buttons, [this] { handlePost(); }, [this] { endDialog(IDCANCEL); });
	postButton = dialogButtons.first;
	postButton->setText(T_("Post"));
	postButton->setEnabled(false);

	subject->onUpdated([this] { updateState(); });
	body->onUpdated([this] { updateState(); });
	richText->onClicked([this] { updateState(); });
	editRichText->onClicked([this] { handleEditRichText(); });

	subject->setText(initialSubject);
	setText(parentTTH.empty() ? T_("New BBS thread") : T_("Reply to BBS post"));
	layout();
	centerWindow();
	updateState();
	if(parentTTH.empty()) subject->setFocus(); else body->setFocus();
	return false;
}

void BBSPostDlg::updateState() {
	ready = false;
	postButton->setEnabled(false);
	editRichText->setEnabled(richText->getChecked());

	try {
		auto subjectText = Text::fromT(subject->getText());
		auto bodyText = Text::fromT(body->getText());
		const auto isRichText = richText->getChecked();
		if(isRichText) {
			auto prepared = bodyText;
			if(!RichText::prepareOutgoingMessage(prepared, true, hubUrl)) {
				validation->setText(T_("The rich-text body is invalid, unsafe, too large, or references an unavailable attachment."));
				return;
			}
			bodyText = std::move(prepared);
		}

		string raw;
		string error;
		BBSDocument document;
		if(!BBSManager::composeDocument(string(39, 'A'), parentTTH, subjectText, bodyText, isRichText, GET_TIME(), raw, document, error)) {
			validation->setText(Text::toT(error));
			return;
		}
		if(maxSize < 0 || document.size > maxSize) {
			validation->setText(Text::toT(str(F_("The post is %1% bytes; this board accepts at most %2% bytes") % document.size % maxSize)));
			return;
		}

		ready = true;
		postButton->setEnabled(true);
		validation->setText(Text::toT(str(F_("Ready: %1% of %2% bytes") % document.size % maxSize)));
	} catch(const Exception& e) {
		validation->setText(Text::toT(e.getError()));
	} catch(...) {
		validation->setText(T_("The draft could not be validated."));
	}
}

void BBSPostDlg::handleEditRichText() {
	RichTextEditorDlg dialog(this, hubUrl, body->getText(), T_("Use in post"));
	if(dialog.run() != IDOK) return;
	body->setText(dialog.getText());
	richText->setChecked(true);
	updateState();
}

void BBSPostDlg::handlePost() {
	updateState();
	if(!ready) return;
	try {
		resultSubject = Text::fromT(subject->getText());
		resultBody = Text::fromT(body->getText());
		resultRichText = richText->getChecked();
		endDialog(IDOK);
	} catch(const Exception& e) {
		validation->setText(Text::toT(e.getError()));
	} catch(...) {
		validation->setText(T_("The draft could not be encoded as UTF-8."));
	}
}
