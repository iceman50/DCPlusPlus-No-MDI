#include "testbase.h"

#include <dcpp/SimpleXMLReader.h>
#include <dcpp/SimpleXML.h>
#include <dcpp/Text.h>
#include <unordered_map>

using namespace dcpp;

typedef std::unordered_map<std::string, int> Counter;

class Collector : public SimpleXMLReader::CallBack {
public:

	void startTag(const std::string& name, dcpp::StringPairList& attribs, bool simple) {
		if(simple) {
			simpleTags[name]++;
		} else {
			startTags[name]++;
		}

		for(auto& i: attribs) {
			attribKeys[i.first]++;
			attribValues[i.second]++;
		}
	}

	void data(const std::string& data) {
		dataValues[data]++;
	}

	void endTag(const std::string& name) {
		endTags[name]++;
	}

	Counter simpleTags;
	Counter startTags;
	Counter endTags;
	Counter attribKeys;
	Counter attribValues;
	Counter dataValues;
};

TEST(testxml, test_simple)
{
	Collector collector;
    SimpleXMLReader reader(&collector);

    const char xml[] = "<?xml version='1.0' encoding='utf-8' ?><complex a='1'> data <simple b=\"2\"/><complex2> data </complex2></complex>";
    for(size_t i = 0, iend = sizeof(xml); i < iend; ++i) {
    	reader.parse(xml + i, 1);
    }

    ASSERT_EQ(collector.simpleTags["simple"], 1);
    ASSERT_EQ(collector.startTags["complex"], 1);
    ASSERT_EQ(collector.endTags["complex"], 1);
    ASSERT_EQ(collector.attribKeys["a"], 1);
    ASSERT_EQ(collector.attribValues["1"], 1);
    ASSERT_EQ(collector.attribKeys["b"], 1);
    ASSERT_EQ(collector.attribValues["2"], 1);
    ASSERT_EQ(collector.startTags["complex2"], 1);
    ASSERT_EQ(collector.endTags["complex2"], 1);
    ASSERT_EQ(collector.dataValues[" data "], 2);
}

TEST(testxml, test_entref)
{
	Collector collector;
	SimpleXMLReader reader(&collector);

    const char xml[] = "<root ab='&apos;&amp;&quot;'>&lt;&gt;</root>";
    for(size_t i = 0, iend = sizeof(xml); i < iend; ++i) {
    	reader.parse(xml + i, 1);
    }

    ASSERT_EQ(collector.startTags["root"], 1);
    ASSERT_EQ(collector.endTags["root"], 1);
    ASSERT_EQ(collector.attribKeys["ab"], 1);
    ASSERT_EQ(collector.attribValues["'&\""], 1);
    ASSERT_EQ(collector.dataValues["<>"], 1);
}

TEST(testxml, test_comment)
{
	Collector collector;
	SimpleXMLReader reader(&collector);

    const char xml[] = "<root><!-- comment <i>,;&--></root>";
    for(size_t i = 0, iend = sizeof(xml); i < iend; ++i) {
    	reader.parse(xml + i, 1);
    }

    ASSERT_EQ(collector.startTags["root"], 1);
    ASSERT_EQ(collector.endTags["root"], 1);
}

TEST(testxml, test_cdata)
{
	Collector collector;
	SimpleXMLReader reader(&collector);

    const char xml[] = "<root><![CDATA[Within this Character Data block I can use double dashes as much as I want (along with <, &, ', and \") ... however, I can't use the CEND sequence (if I need to use it I must escape one of the brackets or the greater-than sign).]]></root>";
    for(size_t i = 0, iend = sizeof(xml); i < iend; ++i) {
    	reader.parse(xml + i, 1);
    }

    ASSERT_EQ(collector.startTags["root"], 1);
    ASSERT_EQ(collector.endTags["root"], 1);
}

#include <dcpp/File.h>

TEST(testxml, test_file)
{
	if(File::getSize("test.xml") != -1) {
		Collector collector;
		SimpleXMLReader reader(&collector);
		File f("test.xml", File::READ, File::OPEN);
		reader.parse(f);
	} else {
		SUCCEED() << "test.xml not found";
	}
}

TEST(testxml, test_utf_validation)
{
	const char xml[] = "<Hub Name='\xc3Name'/>";

	Collector collector;
	SimpleXMLReader reader(&collector, SimpleXMLReader::FLAG_REPLACE_INVALID_UTF8);

	for (size_t i = 0, iend = sizeof(xml); i < iend; ++i) {
		reader.parse(xml + i, 1);
	}

	const auto expected = Text::sanitizeUtf8("\xc3Name");
	ASSERT_EQ(collector.attribValues[expected], 1);
}

TEST(testxml, test_selected_child_children)
{
	SimpleXML xml;
	xml.addTag("Favorites");
	xml.stepIn();
	xml.addTag("Hubs");
	xml.stepIn();

	xml.addTag("Hub");
	xml.addChildAttrib("Server", string("adc://first.example"));
	xml.addChildAttrib("ShareProfile", true);
	xml.addChildTag("ShareDirectory", "C:\\First\\");
	xml.addChildTag("ShareDirectory", "D:\\Second\\");

	xml.addTag("Hub");
	xml.addChildAttrib("Server", string("adc://second.example"));
	xml.addChildAttrib("ShareProfile", true);

	xml.stepOut();
	xml.addTag("Users");
	xml.stepOut();

	SimpleXML loaded;
	loaded.fromXML(xml.toXML());
	ASSERT_TRUE(loaded.findChild("Favorites"));
	loaded.stepIn();
	ASSERT_TRUE(loaded.findChild("Hubs"));
	loaded.stepIn();

	ASSERT_TRUE(loaded.findChild("Hub"));
	auto directories = loaded.getChildDataList("ShareDirectory");
	ASSERT_EQ(directories.size(), 2);
	ASSERT_EQ(directories[0], "C:\\First\\");
	ASSERT_EQ(directories[1], "D:\\Second\\");

	ASSERT_TRUE(loaded.findChild("Hub"));
	ASSERT_TRUE(loaded.getBoolChildAttrib("ShareProfile"));
	ASSERT_TRUE(loaded.getChildDataList("ShareDirectory").empty());

	loaded.stepOut();
	loaded.resetCurrentChild();
	ASSERT_TRUE(loaded.findChild("Users"));
}
