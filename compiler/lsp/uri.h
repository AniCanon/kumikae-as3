// File paths and the `file:` URIs an editor names them by.
#pragma once

#include "paths.h"

#include <string>
#include <string_view>

namespace kas3 {

// "/a b/c.as" for "file:///a%20b/c.as", spelled as generic_path() spells it. Empty if the URI is not a
// `file:` one.
std::string path_from_uri(std::string_view p_uri, PathStyle p_style = HOST_PATHS);
// "file:///a%20b/c.as" for "/a b/c.as".
std::string uri_from_path(std::string_view p_path, PathStyle p_style = HOST_PATHS);

} // namespace kas3
