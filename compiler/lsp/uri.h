// File paths and the `file:` URIs an editor names them by.
#pragma once

#include <string>
#include <string_view>

namespace kas3 {

// "/a b/c.as" for "file:///a%20b/c.as". Empty if the URI is not a `file:` one.
std::string path_from_uri(std::string_view p_uri);
// "file:///a%20b/c.as" for "/a b/c.as".
std::string uri_from_path(std::string_view p_path);

} // namespace kas3
