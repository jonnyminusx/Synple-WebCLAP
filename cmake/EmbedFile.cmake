# Generates a C++ source embedding INPUT as a byte array.
# Usage: cmake -DINPUT=<file> -DOUTPUT=<cpp> -P EmbedFile.cmake
file(READ "${INPUT}" HEX_CONTENT HEX)
string(LENGTH "${HEX_CONTENT}" HEX_LENGTH)
math(EXPR BYTE_COUNT "${HEX_LENGTH} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX_CONTENT}")

file(WRITE "${OUTPUT}"
"#include \"UiResources.h\"

namespace synple
{

const unsigned char kUiHtml[]{${BYTES}};
const std::size_t kUiHtmlSize{${BYTE_COUNT}};

} // namespace synple
")
