#include "testbase.h"

#include <dcpp/Text.h>
#include <dcpp/Util.h>

using namespace dcpp;

TEST(testtext, test_tolower)
{
	Text::initialize();

	ASSERT_EQ("abcd1", Text::toLower("ABCd1"));

#ifdef _WIN32
	ASSERT_EQ(_T("abcd1"), Text::toLower(_T("ABCd1")));
#endif

	ASSERT_EQ('a', Text::toLower('A'));
}

TEST(testtext, test_trim_without_system_locale)
{
	string narrow = " \t\r\nvalue\f\v ";
	Util::trim(narrow);
	ASSERT_EQ("value", narrow);

	std::wstring wide = L" \t\r\nvalue\f\v ";
	Util::trim(wide);
	ASSERT_EQ(L"value", wide);
}
