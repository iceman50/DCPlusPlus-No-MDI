/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdinc.h"
#include "Download.h"

#include "UserConnection.h"
#include "QueueItem.h"
#include "HashManager.h"
#include "SettingsManager.h"
#include "MerkleCheckOutputStream.h"
#include "MerkleTreeOutputStream.h"
#include "File.h"
#include "FilteredFile.h"
#include "ZUtils.h"
#include "ZstdUtils.h"

namespace dcpp {

namespace {
// Retain the received frame while validating/decompressing into a bounded discard
// sink. Transfer progress is measured in XML bytes, disk writes in frame bytes.
class ZstdListOutputStream : public OutputStream {
	class Sink : public OutputStream {
	public:
		size_t write(const void*, size_t len) override { return len; }
		size_t flush() override { return 0; }
	} sink;
	std::unique_ptr<OutputStream> file;
	LimitedOutputStream<false> limit;
	FilteredOutputStream<UnZstdFilter, false> decoder;
public:
	ZstdListOutputStream(OutputStream* output, int64_t bytes) : file(output), limit(&sink, bytes), decoder(&limit) { }
	size_t write(const void* data, size_t len) override {
		const auto produced = decoder.write(data, len);
		file->write(data, len);
		return produced;
	}
	size_t flush() override { decoder.flush(); return file->flush(); }
	bool eof() override { return decoder.eof(); }
};
}

Download::Download(UserConnection& conn, QueueItem& qi) noexcept : Transfer(conn, qi.getTarget(), qi.getTTH()),
	tempTarget(qi.getTempTarget()), treeValid(false)
{
	conn.setDownload(this);

	if(qi.isSet(QueueItem::FLAG_PARTIAL_LIST)) {
		setType(TYPE_PARTIAL_LIST);
	} else if(qi.isSet(QueueItem::FLAG_USER_LIST)) {
		setType(TYPE_FULL_LIST);
	}
	if(qi.isSet(QueueItem::FLAG_RECURSIVE_LIST)) {
		setFlag(FLAG_RECURSIVE);
	}

	if(qi.getSize() != -1) {
		if(HashManager::getInstance()->getTree(getTTH(), tt)) {
			setTreeValid(true);
			setSegment(qi.getNextSegment(getTigerTree().getBlockSize(), conn.getChunkSize()));
		} else if(conn.supportsTrees() && !qi.getSource(conn.getUser())->isSet(QueueItem::Source::FLAG_NO_TREE) && qi.getSize() > HashManager::MIN_BLOCK_SIZE) {
			// Get the tree unless the file is small (for small files, we'd probably only get the root anyway)
			setType(TYPE_TREE);
			tt.setFileSize(qi.getSize());
			setSegment(Segment(0, -1));
		} else {
			// Use the root as tree to get some sort of validation at least...
			tt = TigerTree(qi.getSize(), qi.getSize(), getTTH());
			setTreeValid(true);
			setSegment(qi.getNextSegment(getTigerTree().getBlockSize(), 0));
		}

		if(getType() == TYPE_FILE && getSegment().getSize() >= 0 &&
			(getStartPos() != 0 || getSegment().getSize() != qi.getSize()))
		{
			setFlag(FLAG_CHUNKED);
		}
	}
}

Download::~Download() {
	getUserConnection().setDownload(0);
}

AdcCommand Download::getCommand(bool zlib, bool zstd) {
	zstdRequested = SETTING(COMPRESS_TRANSFERS) && zstd && (SETTING(PREFER_ZSTD) || !zlib);
	if(zstdRequested && getType() == TYPE_FULL_LIST) unsetFlag(FLAG_XML_BZ_LIST);
	AdcCommand cmd(AdcCommand::CMD_GET);

	cmd.addParam(Transfer::names[getType()]);

	if(getType() == TYPE_PARTIAL_LIST) {
		cmd.addParam(Util::toAdcFile(getTempTarget()));
	} else if(getType() == TYPE_FULL_LIST) {
		if(isSet(Download::FLAG_XML_BZ_LIST)) {
			cmd.addParam(USER_LIST_NAME_BZ);
		} else {
			cmd.addParam(USER_LIST_NAME);
		}
	} else {
		cmd.addParam("TTH/" + getTTH().toBase32());
	}

	cmd.addParam(Util::toString(getStartPos()));
	cmd.addParam(Util::toString(getSize()));

	// A BZip2 file list is already compressed. Wrapping it in ADC's optional
	// ZL1 stream saves virtually nothing and has historically exposed broken
	// nested-compression implementations in otherwise compatible peers.
	if(zstdRequested) {
		cmd.addParam("ZS1");
	} else if(zlib && SETTING(COMPRESS_TRANSFERS) &&
		!(getType() == TYPE_FULL_LIST && isSet(Download::FLAG_XML_BZ_LIST)))
	{
		cmd.addParam("ZL1");
	}
	if(getType() == TYPE_PARTIAL_LIST && isSet(FLAG_RECURSIVE)) {
		cmd.addParam("RE1");
	}

	requestedFile = cmd.getParam(1);
	return cmd;
}

void Download::getParams(const UserConnection& aSource, ParamMap& params) {
	Transfer::getParams(aSource, params);
	params["target"] = getPath();
}

void Download::appendFlags(StringList& flags) const {
	Transfer::appendFlags(flags);
	if(isSet(FLAG_TTH_CHECK)) {
		flags.emplace_back("T");
	}
	if(isSet(FLAG_ZSTD)) flags.emplace_back("ZS");
	if(isSet(FLAG_ZDOWNLOAD)) {
		flags.emplace_back("Z");
	}
	if(isSet(FLAG_CHUNKED)) {
		flags.emplace_back("C");
	}
}

string Download::getTargetFileName() const {
	return Util::getFileName(getPath());
}

const string& Download::getDownloadTarget() const {
	return (getTempTarget().empty() ? getPath() : getTempTarget());
}

void Download::open(int64_t bytes, bool z, bool zstd) {
	if(zstd && getType() == TYPE_FULL_LIST) {
		unsetFlag(FLAG_XML_BZ_LIST);
		setFlag(FLAG_XML_ZST_LIST);
	}
	if(getType() == Transfer::TYPE_FILE) {
		auto target = getDownloadTarget();
		auto fullSize = tt.getFileSize();

		if(getSegment().getStart() > 0) {
			if(File::getSize(target) != fullSize) {
				// When trying the download the next time, the resume pos will be reset
				throw Exception(_("Target file is missing or wrong size"));
			}
		} else {
			File::ensureDirectory(target);
		}

		unique_ptr<File> f(new File(target, File::WRITE, File::OPEN | File::CREATE | File::SHARED));

		if(f->getSize() != fullSize) {
			f->setSize(fullSize);
		}

		f->setPos(getSegment().getStart());
		output = move(f);
		tempTarget = target;
	} else if(getType() == Transfer::TYPE_FULL_LIST) {
		auto target = getPath();
		File::ensureDirectory(target);

		if(isSet(Download::FLAG_XML_ZST_LIST)) {
			target += ".xml.zst";
		} else if(isSet(Download::FLAG_XML_BZ_LIST)) {
			target += ".xml.bz2";
		} else {
			target += ".xml";
		}

		output.reset(new File(target, File::WRITE, File::OPEN | File::TRUNCATE | File::CREATE));
		tempTarget = target;
	} else if(getType() == Transfer::TYPE_PARTIAL_LIST) {
		output.reset(new StringRefOutputStream(pfs));
	} else if(getType() == Transfer::TYPE_TREE) {
		output.reset(new MerkleTreeOutputStream<TigerTree>(tt));
	}

	if((getType() == Transfer::TYPE_FILE || getType() == Transfer::TYPE_FULL_LIST) && SETTING(BUFFER_SIZE) > 0 ) {
		output.reset(new BufferedOutputStream<true>(output.release()));
	}

	if(isSet(FLAG_XML_ZST_LIST)) {
		setFlag(FLAG_ZSTD);
		output.reset(new ZstdListOutputStream(output.release(), bytes));
		return;
	}

	if(getType() == Transfer::TYPE_FILE) {
		typedef MerkleCheckOutputStream<TigerTree, true> MerkleStream;

		output.reset(new MerkleStream(tt, output.release(), getStartPos()));
		setFlag(Download::FLAG_TTH_CHECK);
	}

	// Check that we don't get too many bytes
	output.reset(new LimitedOutputStream<true>(output.release(), bytes));

	if(zstd) {
		setFlag(Download::FLAG_ZSTD);
		output.reset(new FilteredOutputStream<UnZstdFilter, true>(output.release()));
	} else if(z) {
		setFlag(Download::FLAG_ZDOWNLOAD);
		output.reset(new FilteredOutputStream<UnZFilter, true>(output.release()));
	}
}

void Download::close()
{
	output.reset();
}

bool Download::hasValidFileListSignature() const {
	if(getType() != TYPE_FULL_LIST || tempTarget.empty()) {
		return true;
	}

	try {
		File file(tempTarget, File::READ, File::OPEN);
		const auto signature = file.read(5);
		if(isSet(FLAG_XML_ZST_LIST)) return signature.size() >= 4 && signature.compare(0, 4, "\x28\xb5\x2f\xfd", 4) == 0;
		if(isSet(Download::FLAG_XML_BZ_LIST)) {
			// BZip2 streams begin with "BZh" followed by a block-size digit 1-9.
			return signature.size() >= 4 && signature.compare(0, 3, "BZh") == 0 &&
				signature[3] >= '1' && signature[3] <= '9';
		}

		// Uncompressed ADC lists generated by DC clients begin with an XML declaration.
		return signature == "<?xml";
	} catch(const Exception&) {
		return false;
	}
}

} // namespace dcpp
