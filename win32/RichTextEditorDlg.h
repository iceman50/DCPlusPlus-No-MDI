/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_RICH_TEXT_EDITOR_DLG_H
#define DCPLUSPLUS_WIN32_RICH_TEXT_EDITOR_DLG_H

#include <dcpp/ShareManager.h>
#include <dcpp/typedefs.h>

#include <atomic>
#include <memory>
#include <optional>

#include "GridDialog.h"

/** Source-preserving ADC RTF0 editor with a separate rendered preview. */
class RichTextEditorDlg : public GridDialog {
public:
	RichTextEditorDlg(dwt::Widget* parent, const string& hubUrl, const tstring& initialText, const tstring& useCaption = Util::emptyStringT);
	~RichTextEditorDlg();

	const tstring& getText() const { return result; }

private:
	string hubUrl;
	tstring initialText;
	tstring result;
	tstring useCaption;

	TextBoxPtr source;
	RichTextBoxPtr preview;
	LabelPtr validation;
	ButtonPtr useButton;
	bool ready;
	size_t pendingAttachments;
	std::shared_ptr<std::atomic<bool>> editorAlive;
	vector<uint64_t> attachmentRequests;

	struct AttachmentBatch {
		vector<string> names;
		vector<bool> inlineMedia;
		vector<std::optional<ShareManager::ChatAttachmentResult>> results;
		size_t remaining = 0;
	};

	bool handleInitDialog();
	void updatePreview();
	void handleUse();

	void wrapSelection(const tstring& before, const tstring& after, const tstring& placeholder);
	void insertBlock(const tstring& block);
	void insertAttachment(bool inlineMedia);
	bool handleDroppedFiles(const TStringList& files);
	void prepareAttachments(const StringList& paths, const vector<bool>& inlineMedia);
	void finishAttachments(const std::shared_ptr<AttachmentBatch>& batch);
	void removeAttachmentRequest(uint64_t requestId);
	void showAttachmentError(const tstring& message);
};

#endif // !defined(DCPLUSPLUS_WIN32_RICH_TEXT_EDITOR_DLG_H)
