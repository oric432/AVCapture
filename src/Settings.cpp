#include "Settings.hpp"

#include <cctype>
#include <charconv>

namespace AVCapture {

Error::Result<size_t> Settings::parse_byte_size(std::string_view size_str) {
  auto fail = [&](std::string_view why) {
    std::ostringstream oss;
    oss << "Invalid byte size '" << size_str << "': " << why;
    return std::unexpected(Error::make_error().with_context(oss.str()));
  };

  size_t digits = 0;
  while (digits < size_str.size() &&
         std::isdigit(static_cast<unsigned char>(size_str[digits])) != 0) {
    ++digits;
  }
  if (digits == 0) {
    return fail("missing numeric value");
  }

  uint64_t value = 0;
  auto [ptr, ec] =
      std::from_chars(size_str.data(), size_str.data() + digits, value);
  if (ec != std::errc{}) {
    return fail("bad numeric value");
  }

  std::string unit{size_str.substr(digits)};
  for (char &c : unit) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  uint64_t multiplier = 0;
  if (unit.empty() || unit == "b") {
    multiplier = 1;
  } else if (unit == "kb") {
    multiplier = 1024ULL;
  } else if (unit == "mb") {
    multiplier = 1024ULL * 1024ULL;
  } else if (unit == "gb") {
    multiplier = 1024ULL * 1024ULL * 1024ULL;
  } else {
    return fail("unknown unit '" + unit + "'");
  }

  return static_cast<size_t>(value * multiplier);
}

void Settings::merge_into(toml::table &dst, const toml::table &src) {
  // Recursive merge:
  for (const auto &[k, v] : src) {
    if (const auto *src_tbl = v.as_table()) {
      if (toml::node *existing = dst.get(k);
          (existing != nullptr) && existing->is_table()) {
        merge_into(*existing->as_table(), *src_tbl);
      } else {
        dst.insert_or_assign(k, toml::table{*src_tbl});
      }
    } else {
      dst.insert_or_assign(k, v);
    }
  }
}

Error::Result<Settings> Settings::load(std::string_view file_path) {
  // 1) Parse defaults (schema baseline)
  auto def_res = toml::parse(std::string(kDefaultsToml));
  if (!def_res) {
    std::string msg = "Internal defaults TOML parse failed: ";
    msg += def_res.error().description();
    return std::unexpected(Error::make_error().with_context(msg));
  }

  toml::table cfg = def_res.table();

  // 2) Parse user file
  auto file_res = toml::parse_file(std::string(file_path));
  if (!file_res) {
    std::string msg = "Settings file parse failed: ";
    msg += file_res.error().description();
    return std::unexpected(Error::make_error().with_context(msg));
  }

  // 3) Merge file onto defaults
  merge_into(cfg, file_res.table());

  return Settings{cfg};
}

std::string Settings::dump() const {
  std::ostringstream oss;
  oss << table_;
  return oss.str();
}
} // namespace AVCapture
