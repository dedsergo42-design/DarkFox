#pragma once

#define NOMINMAX

#include <windows.h>
#include <shellapi.h>   // ← ДОБАВИТЬ (для ShellExecuteW)
#include <ShlObj.h>     // ← ДОБАВИТЬ (для SHGetFolderPathW)
#include <filesystem>   // ← ДОБАВИТЬ (на всякий случай)

#include "nlohmann/json.hpp"
#include "lz4/lz4.h"
#include "xdraw/xui/xui.hpp"

namespace config {

	enum class field_type : std::uint8_t {
		setting,
		bool_val,
		int_val,
		uint8_val,
		float_val,
		color,
		float3,
		bool_array,
		string_val,
		custom
	};

	struct float3 {
		float x {}, y {}, z {};
	};

	struct field {
		std::uint32_t key;
		field_type type;
		void* ptr;
		std::uint32_t count;
	};

	struct custom_field {
		virtual ~custom_field () = default;
		virtual nlohmann::json serialize () const = 0;
		virtual void deserialize (const nlohmann::json& j) = 0;
	};

	namespace detail {

		struct config_registry {
			std::vector<field> fields {};
			nlohmann::json defaults {};
			// Snapshot of compile-time defaults before apply_blank_profile; legacy share codes (delta v1)
			// encode diffs against this baseline, not against the blank defaults snapshot.
			nlohmann::json factory_defaults {};
		};

		inline config_registry& get_registry () {
			static config_registry r {};
			return r;
		}

		inline std::uint32_t make_key (std::string_view category, std::string_view name) {
			char buf [256] {};
			const auto cat_len = std::min (category.size (), std::size_t {120});
			const auto name_len = std::min (name.size (), std::size_t {120});

			std::memcpy (buf, category.data (), cat_len);
			buf [cat_len] = '.';
			std::memcpy (buf + cat_len + 1, name.data (), name_len);
			buf [cat_len + 1 + name_len] = '\0';

			return xui::fnv1a (buf);
		}

		inline void register_field (field f) {
			get_registry ().fields.push_back (f);
		}

		inline void unregister_ptr (void* ptr) {
			auto& v = get_registry ().fields;
			v.erase (std::remove_if (v.begin (), v.end (), [ptr] (const field& f) { return f.ptr == ptr; }), v.end ());
		}

	} // namespace detail

	template <typename T>
	struct val {
		T value {};

		val () = default;

		explicit val (T v) : value {v} {}

		val (T v, std::string_view category, std::string_view name) : value {v} {
			reg (category, name);
		}

		void reg (std::string_view category, std::string_view name) {
			constexpr auto ft = [] () {
				if constexpr (std::is_same_v<T, bool>)              return field_type::bool_val;
				else if constexpr (std::is_same_v<T, int>)          return field_type::int_val;
				else if constexpr (std::is_same_v<T, float>)        return field_type::float_val;
				else if constexpr (std::is_same_v<T, std::uint8_t>) return field_type::uint8_val;
				else static_assert(!sizeof (T), "unsupported type for config::val");
			}();

			detail::register_field ({.key = detail::make_key (category, name), .type = ft, .ptr = &this->value, .count = 1});
		}

		operator T& () noexcept {
			return value;
		}
		operator const T& () const noexcept {
			return value;
		}
		val& operator=(T v) noexcept {
			value = v; return *this;
		}
	};

	struct col {
		xdraw::color value {};

		col () = default;

		explicit col (xdraw::color v) : value {v} {}

		col (xdraw::color v, std::string_view category, std::string_view name) : value {v} {
			reg (category, name);
		}

		void reg (std::string_view category, std::string_view name) {
			detail::register_field ({.key = detail::make_key (category, name), .type = field_type::color, .ptr = &this->value, .count = 1});
		}

		operator xdraw::color& () noexcept {
			return value;
		}
		operator const xdraw::color& () const noexcept {
			return value;
		}
		col& operator=(const xdraw::color& v) noexcept {
			value = v; return *this;
		}
	};

	struct vec3 {
		float3 value {};

		vec3 () = default;

		explicit vec3 (float3 v) : value {v} {}

		vec3 (float3 v, std::string_view category, std::string_view name) : value {v} {
			reg (category, name);
		}

		void reg (std::string_view category, std::string_view name) {
			detail::register_field ({.key = detail::make_key (category, name), .type = field_type::float3, .ptr = &this->value, .count = 1});
		}

		operator float3& () noexcept {
			return value;
		}
		operator const float3& () const noexcept {
			return value;
		}
		vec3& operator=(const float3& v) noexcept {
			value = v; return *this;
		}
	};

	template <typename E>
	struct enm {
		static_assert(std::is_enum_v<E>, "enm requires an enum type");

		E value {};

		enm () = default;

		explicit enm (E v) : value {v} {}

		enm (E v, std::string_view category, std::string_view name) : value {v} {
			reg (category, name);
		}

		void reg (std::string_view category, std::string_view name) {
			constexpr auto ft = (sizeof (E) == 1) ? field_type::uint8_val : field_type::int_val;

			detail::register_field ({.key = detail::make_key (category, name), .type = ft, .ptr = &this->value, .count = 1});
		}

		operator E& () noexcept {
			return value;
		}
		operator const E& () const noexcept {
			return value;
		}
		enm& operator=(E v) noexcept {
			value = v; return *this;
		}
	};

	template <std::uint32_t N>
	struct bools {
		bool values [N] {};

		bools () = default;

		explicit bools (std::initializer_list<bool> init) {
			auto i {0u};

			for (auto v : init) {
				if (i >= N) {
					break;
				}

				values [i++] = v;
			}
		}

		bools (std::initializer_list<bool> init, std::string_view category, std::string_view name) {
			auto i {0u};

			for (auto v : init) {
				if (i >= N) {
					break;
				}

				values [i++] = v;
			}

			reg (category, name);
		}

		void reg (std::string_view category, std::string_view name) {
			detail::register_field ({.key = detail::make_key (category, name), .type = field_type::bool_array, .ptr = this->values, .count = N});
		}

		bool& operator[](std::size_t i) {
			return values [i];
		}
		const bool& operator[](std::size_t i) const {
			return values [i];
		}
		operator bool* () noexcept {
			return values;
		}
		operator const bool* () const noexcept {
			return values;
		}
	};

	struct str {
		std::string value {};

		str () = default;

		explicit str (std::string_view v) : value {v} {}

		str (std::string_view v, std::string_view category, std::string_view name) : value {v} {
			reg (category, name);
		}

		void reg (std::string_view category, std::string_view name) {
			detail::register_field ({.key = detail::make_key (category, name), .type = field_type::string_val, .ptr = &this->value, .count = 1});
		}

		operator std::string& () noexcept {
			return value;
		}
		operator const std::string& () const noexcept {
			return value;
		}
		str& operator=(const std::string& v) noexcept {
			value = v; return *this;
		}
		str& operator=(std::string_view v) noexcept {
			value = v; return *this;
		}

		[[nodiscard]] const char* c_str () const noexcept {
			return value.c_str ();
		}
		[[nodiscard]] bool empty () const noexcept {
			return value.empty ();
		}
	};

	namespace serial {

		inline nlohmann::json bind_to_json (const xui::bind_info& b) {
			if (b.key == 0) {
				return nullptr;
			}

			return nlohmann::json {{ "k", b.key }, { "m", static_cast<int>(b.mode) }};
		}

		inline void json_to_bind (const nlohmann::json& j, xui::bind_info& b) {
			if (j.is_null ()) {
				b.key = 0;
				b.mode = xui::bind_mode::toggle;
				return;
			}

			b.key = j.value ("k", 0);
			b.mode = static_cast<xui::bind_mode>(j.value ("m", 0));
		}

		inline nlohmann::json field_to_json (const field& f) {
			switch (f.type) {
				case field_type::setting:
				{
					const auto s = static_cast<const xui::setting*>(f.ptr);
					return nlohmann::json {{ "v", s->value }, { "b", bind_to_json (s->bind) }};
				}
				case field_type::bool_val:  return *static_cast<const bool*>(f.ptr);
				case field_type::int_val:   return *static_cast<const int*>(f.ptr);
				case field_type::uint8_val: return *static_cast<const std::uint8_t*>(f.ptr);
				case field_type::float_val: return *static_cast<const float*>(f.ptr);
				case field_type::color:
				{
					const auto& c = *static_cast<const xdraw::color*>(f.ptr);
					return nlohmann::json::array ({c.r, c.g, c.b, c.a});
				}
				case field_type::float3:
				{
					const auto& v = *static_cast<const config::float3*>(f.ptr);
					return nlohmann::json::array ({v.x, v.y, v.z});
				}
				case field_type::bool_array:
				{
					const auto arr = static_cast<const bool*>(f.ptr);
					auto j = nlohmann::json::array ();

					for (std::uint32_t i = 0; i < f.count; ++i) {
						j.push_back (arr [i]);
					}

					return j;
				}
				case field_type::string_val:
				{
					return *static_cast<const std::string*> (f.ptr);
				}
				case field_type::custom:
				{
					const auto c = static_cast<const custom_field*>(f.ptr);
					return c->serialize ();
				}
			}
			return nullptr;
		}

		inline void json_to_field (const nlohmann::json& j, field& f) {
			// A `setting` serializes as {"v":bool,"b":bind}. Documents written by
			// an older build, or hand-edited ones, sometimes carry that wrapper on
			// a field that is now plain (or vice versa) -- the field's type is the
			// authority here, not the document's shape. Without this, a bool_val
			// asked to read {"v":true} throws inside get<bool>(), the catch below
			// swallows it, and the value silently stays at whatever it was. That
			// is a shipped default quietly not applying, which is exactly the kind
			// of bug this whole profile mechanism has to be immune to.
			const auto unwrapped = [ & ] ( ) -> const nlohmann::json& {
				if ( j.is_object ( ) && j.contains ( "v" ) ) {
					return j [ "v" ];
				}
				return j;
			}( );

			try {
				switch (f.type) {
					case field_type::setting:
					{
						auto s = static_cast<xui::setting*>(f.ptr);

						if (j.contains ("v")) {
							s->value = j ["v"].get< bool > ();
						}

						if (j.contains ("b")) {
							json_to_bind (j ["b"], s->bind);
						}

						if (s->bind.key != 0 && s->bind.mode == xui::bind_mode::toggle) {
							s->bind.active = s->value;
						}

						break;
					}
					case field_type::bool_val:  if (unwrapped.is_boolean ()) { *static_cast<bool*>(f.ptr) = unwrapped.get< bool > (); } break;
					case field_type::int_val:   if (unwrapped.is_number ()) { *static_cast<int*>(f.ptr) = unwrapped.get< int > (); } break;
					case field_type::uint8_val: if (unwrapped.is_number ()) { *static_cast<std::uint8_t*>(f.ptr) = unwrapped.get< std::uint8_t > (); } break;
					case field_type::float_val: if (unwrapped.is_number ()) { *static_cast<float*>(f.ptr) = unwrapped.get< float > (); } break;
					case field_type::color:
					{
						auto& c = *static_cast<xdraw::color*>(f.ptr);
						if (j.is_array () && j.size () >= 4) {
							c.r = j [0]; c.g = j [1]; c.b = j [2]; c.a = j [3];
						}

						break;
					}
					case field_type::float3:
					{
						auto& v = *static_cast<config::float3*>(f.ptr);
						if (j.is_array () && j.size () >= 3) {
							v.x = j [0]; v.y = j [1]; v.z = j [2];
						}

						break;
					}
					case field_type::bool_array:
					{
						auto arr = static_cast<bool*>(f.ptr);
						if (j.is_array ()) {
							for (std::uint32_t i = 0; i < std::min< std::uint32_t > (static_cast<std::uint32_t> (j.size ()), f.count); ++i) {
								arr [i] = j [i].get< bool > ();
							}
						}

						break;
					}
					case field_type::string_val:
					{
						if (j.is_string ()) {
							*static_cast<std::string*> (f.ptr) = j.get<std::string> ();
						}
						break;
					}
					case field_type::custom:
					{
						auto c = static_cast<custom_field*>(f.ptr);
						c->deserialize (j);
						break;
					}
				}
			} catch (...) {
			}
		}

	} // namespace serial

	namespace base64 {

		inline constexpr char k_table [] {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};

		inline std::string encode (const std::uint8_t* data, std::size_t len) {
			std::string out;
			out.reserve ((len + 2) / 3 * 4);

			for (std::size_t i = 0; i < len; i += 3) {
				const auto b0 = data [i];
				const auto b1 = (i + 1 < len) ? data [i + 1] : 0;
				const auto b2 = (i + 2 < len) ? data [i + 2] : 0;
				out.push_back (k_table [b0 >> 2]);
				out.push_back (k_table [((b0 & 0x03) << 4) | (b1 >> 4)]);
				out.push_back ((i + 1 < len) ? k_table [((b1 & 0x0F) << 2) | (b2 >> 6)] : '=');
				out.push_back ((i + 2 < len) ? k_table [b2 & 0x3F] : '=');
			}

			return out;
		}

		inline std::string encode (const std::string& s) {
			return encode (reinterpret_cast<const std::uint8_t*>(s.data ()), s.size ());
		}

		inline std::optional<std::vector<std::uint8_t>> decode (std::string_view s) {
			static constexpr auto make_rev = [] () { std::array<std::uint8_t, 256> t {}; t.fill (0xff); for (auto i = 0; i < 64; ++i) t [static_cast<unsigned char> (k_table [i])] = static_cast<std::uint8_t> (i); t ['='] = 0; return t; };
			static constexpr auto k_rev = make_rev ();

			if (s.size () % 4 != 0) {
				return std::nullopt;
			}

			std::vector<std::uint8_t> out;
			out.reserve (s.size () / 4 * 3);

			for (std::size_t i = 0; i < s.size (); i += 4) {
				const auto a = k_rev [static_cast<unsigned char> (s [i])];
				const auto b = k_rev [static_cast<unsigned char> (s [i + 1])];
				const auto c = k_rev [static_cast<unsigned char> (s [i + 2])];
				const auto d = k_rev [static_cast<unsigned char> (s [i + 3])];

				if (a == 0xff || b == 0xff || c == 0xff || d == 0xff) {
					return std::nullopt;
				}

				out.push_back ((a << 2) | (b >> 4));

				if (s [i + 2] != '=') {
					out.push_back (((b & 0x0f) << 4) | (c >> 2));
				}

				if (s [i + 3] != '=') {
					out.push_back (((c & 0x03) << 6) | d);
				}
			}

			return out;
		}

	} // namespace base64

	namespace compress {

		inline std::vector<std::uint8_t> deflate (const std::string& input) {
			const auto bound = LZ4_compressBound (static_cast<int>(input.size ()));
			if (bound <= 0) {
				return {};
			}

			std::vector<std::uint8_t> buf (4 + bound);

			const auto src_size = static_cast<std::uint32_t>(input.size ());
			std::memcpy (buf.data (), &src_size, 4);

			const auto compressed = LZ4_compress_default (input.data (), reinterpret_cast<char*>(buf.data () + 4), static_cast<int>(input.size ()), bound);
			if (compressed <= 0) {
				return {};
			}

			buf.resize (4 + static_cast<std::size_t>(compressed));
			return buf;
		}

		inline std::optional<std::string> inflate (const std::uint8_t* data, std::size_t len) {
			if (len < 4) {
				return std::nullopt;
			}

			std::uint32_t original_size {};
			std::memcpy (&original_size, data, 4);

			if (original_size > 64 * 1024 * 1024) {
				return std::nullopt;
			}

			std::string out (original_size, '\0');

			const auto result = LZ4_decompress_safe (reinterpret_cast<const char*>(data + 4), out.data (), static_cast<int>(len - 4), static_cast<int>(original_size));
			if (result < 0) {
				return std::nullopt;
			}

			out.resize (static_cast<std::size_t> (result));
			return out;
		}

	} // namespace compress
	namespace words {

		inline constexpr const char* k_wordlist [256]
		{
			"thighhighs", "skirt", "softboy", "feral", "twink", "astolfo", "estrogen", "corset",
			"constexpr", "cuddles", "consteval", "atp", "tbh", "ngl", "lol", "mutable_input_history",
			"niche", "asf", "inaccuracy", "spread", "shoot position ring buffer", "buddha_reset_hp", "pseudo code generation failure", "jump -> bugged",
			"subtick", "ts", "etc", "pwned", "on the bin", "missed due to prediction error", "hitchanced", "interp",
			"UwU", "OwO", ">w<", "^w^", ":3", ">:3", "^.^", "^_^",
			".-.", ";-;", "T_T", ":p", ">///<", "-w-", "qwq", "x3",
			"nya", "nyan", "nyaaa", "meow", "kawaii", "sugoi", "baka", "senpai",
			"senpai~", "senpaiii", "oniichan", "ara~", "tsundere", "neko", "mochi", "pocky",
			"hehe", "ehehe", "hihi", "teehee", "bleh", "mlem", "rawr", "rawrr",
			"giggle", "gasp", "whimper", "purr", "squeak", "crybaby", "hmph", "sigh",
			"cutie", "sweetie", "honey", "honeyyy", "darling", "angel", "doll", "kitten",
			"kitty", "bunny", "pup", "goodboy", "goodgirl", "babygirl", "princess", "prettyboy",
			"brat", "bratty", "bratcore", "sassy", "chaotic", "gremlin", "menace", "clingy",
			"needy", "needyyy", "whiny", "pouty", "blushy", "flustered", "shy", "dorky",
			"silly", "goofy", "playful", "teasing", "flirty", "dreamy", "spacey", "smug",
			"cheeky", "mischievous", "sneaky", "dramatic", "extra", "intense", "wild", "untamed",
			"cuddle", "snuggle", "huggy", "kissy", "mwah", "chu", "chuu", "boykissing",
			"nom", "nibble", "bitey", "notice", "please", "gimme", "wanty", "muah",
			"smol", "tiny", "lil", "softie", "eepy", "sleepy", "dozy", "cozy",
			"comfy", "sleepyhead", "zzz", "fragile", "delicate", "precious", "floaty", "dreamer",
			"sparkly", "glittery", "shinyyy", "silky", "pastel", "aesthetic", "vibey", "neon",
			"nightcore", "dreamcore", "weirdcore", "moonlit", "twilight", "spooky", "radiant", "divine",
			"stockings", "choker", "collar", "bell", "ribbon", "bow", "paws", "thighs",
			"blush", "garter", "hello kitty", "strawberry", "peachy", "candy", "sugar", "boba",
			"someone tried to leave a 'was here'", "weener", "DarkFox.cat core1!!11", "spiderman 3", "ironman 3", "//^_^//", "wiggle", "tappy",
			"resolver", "antiaim", "fakelag", "doubletap", "hideshot", "onshot", "hitchance", "multipoint",
			"safepoint", "baim", "lagcomp", "backtrack", "tickbase", "exploit", "autopeek", "quickpeek",
			"duckpeek", "autostop", "wallbang", "cfg diff", "force body", "min damage", "silent aim", "prediction error",
			"skill issue", "gg go next", "no shot", "actual bot", "cope", "ratio", "touch grass", "delulu",
			"brainrot", "copium", "mald", "unhinged", "pilled", "mid", "cooked", "glazing",
			"aura", "negative aura", "rent free", "rolled", "obsessed", "addictive", "hypnotic", "alluring",
			"chaoticgood", "chaoticneutral", "tempting", "sinful", "naughty", "heavenly", "shattered", "broken"
		};

		namespace detail {

			inline std::unordered_map<std::string_view, std::uint8_t>& get_lookup () {
				static auto map = [] () {
					std::unordered_map<std::string_view, std::uint8_t> m;
					m.reserve (256);

					for (auto i = 0; i < 256; ++i) {
						m [k_wordlist [i]] = static_cast<std::uint8_t> (i);
					}

					return m;
				}();

				return map;
			}

		} // namespace detail

		inline std::string encode (const std::uint8_t* data, std::size_t len) {
			std::string out;
			out.reserve (len * 7);

			for (std::size_t i = 0; i < len; ++i) {
				if (i > 0) {
					out.push_back ('|');
				}

				out.append (k_wordlist [data [i]]);
			}

			return out;
		}

		inline std::optional<std::vector<std::uint8_t>> decode (std::string_view input) {
			const auto& lookup = detail::get_lookup ();
			std::vector<std::uint8_t> out;
			out.reserve (input.size () / 5);

			std::size_t pos {};

			while (pos < input.size ()) {
				const auto start = pos;

				while (pos < input.size () && input [pos] != '|') {
					++pos;
				}

				const auto word = input.substr (start, pos - start);
				auto it = lookup.find (word);

				if (it == lookup.end ()) {
					return std::nullopt;
				}

				out.push_back (it->second);

				if (pos < input.size ()) {
					++pos;
				}
			}

			return out;
		}

	} // namespace words

	inline constexpr int k_version {2};
	inline constexpr int k_delta_blank_baseline_version {2};
	inline constexpr char k_share_magic [] {"AC"};

	// The shipped profile (see core/defaults.hpp). Declared ahead of
	// apply_blank_profile(), which consults it, and defined in full further down
	// next to the rest of the persistence layer.
	namespace defaults {

		inline std::string& get ( ) {
			static std::string text {};
			return text;
		}

		// Parsed copy of `get()`, memoised. apply_blank_profile() asks for it once
		// per field, so re-parsing there would be a thousand parses at boot.
		//
		// The cache is invalidated when the text changes rather than being built
		// on first use: capture_default_profile() runs before the first caller
		// here, but a future edit that reordered them would otherwise leave a
		// stale empty document cached forever, silently disabling the profile.
		inline const nlohmann::json& parsed ( ) {
			static std::string cached_text {};
			static nlohmann::json doc {};

			const auto& text = get ( );
			if ( text != cached_text ) {
				cached_text = text;
				doc = text.empty ( ) ? nlohmann::json { }
					: nlohmann::json::parse ( text, nullptr, false );

				if ( doc.is_discarded ( ) ) {
					doc = nlohmann::json { };
				}
			}

			return doc;
		}

	} // namespace defaults

	inline void apply_blank_profile () {
		auto& reg = detail::get_registry ();

		for (auto& f : reg.fields) {
			char key_str [12];
			std::snprintf (key_str, sizeof (key_str), "%08x", f.key);

			// A shipped profile wins over the zero-fill. Consulting `defaults`
			// here is also what makes it the registry's reset target: this
			// function's whole job is to install the "fresh install" state that
			// reset_defaults() and every files::load() fall back to, and after
			// install_default_profile() the shipped document *is* that state.
			// Without this branch the fallback would be these hand-written
			// zeroes, silently disagreeing with what the cheat booted with.
			if (const auto& doc = defaults::parsed (); doc.is_object () && doc.contains ("fields")) {
				const auto& fields = doc ["fields"];
				if (fields.is_object () && fields.contains (key_str)) {
					serial::json_to_field (fields [key_str], f);
					continue;
				}
			}

			switch (f.type) {
				case field_type::setting:
				{
					auto* s = static_cast<xui::setting*>(f.ptr);
					s->value = false;
					s->bind.key = 0;
					s->bind.mode = xui::bind_mode::toggle;
					s->bind.active = false;
					break;
				}
				case field_type::bool_val:
					*static_cast<bool*>(f.ptr) = false;
					break;
				case field_type::int_val:
					*static_cast<int*>(f.ptr) = 0;
					break;
				case field_type::uint8_val:
					*static_cast<std::uint8_t*>(f.ptr) = 0;
					break;
				case field_type::float_val:
					*static_cast<float*>(f.ptr) = 0.0f;
					break;
				case field_type::color:
					*static_cast<xdraw::color*>(f.ptr) = xdraw::color {0, 0, 0, 0};
					break;
				case field_type::float3:
					*static_cast<float3*>(f.ptr) = float3 { };
					break;
				case field_type::string_val:
					static_cast<std::string*>(f.ptr)->clear ();
					break;
				case field_type::bool_array:
				{
					auto* arr = static_cast<bool*>(f.ptr);
					for (std::uint32_t i = 0; i < f.count; ++i) {
						arr [i] = false;
					}
					break;
				}
				case field_type::custom:
					static_cast<custom_field*> (f.ptr)->deserialize (nlohmann::json::object ());
					break;
			}
		}
	}

	inline void initialize () {
		for (auto s : xui::binds::all ()) {
			if (!s || s->category.empty () || s->name.empty ()) {
				continue;
			}

			detail::register_field ({.key = detail::make_key (s->category, s->name), .type = field_type::setting, .ptr = s, .count = 1});
		}

		auto& reg = detail::get_registry ();
		{
			auto factory_obj = nlohmann::json::object ();
			for (const auto& f : reg.fields) {
				char key_str [12];
				std::snprintf (key_str, sizeof (key_str), "%08x", f.key);
				factory_obj [key_str] = serial::field_to_json (f);
			}
			reg.factory_defaults = factory_obj;
			reg.defaults = std::move (factory_obj);
		}
	}

	inline nlohmann::json to_json () {
		auto& reg = detail::get_registry ();
		auto fields_obj = nlohmann::json::object ();

		for (const auto& f : reg.fields) {
			char key_str [12];
			std::snprintf (key_str, sizeof (key_str), "%08x", f.key);
			fields_obj [key_str] = serial::field_to_json (f);
		}

		return nlohmann::json {{ "version", k_version }, { "fields", std::move (fields_obj) }};
	}

	inline bool from_json (const nlohmann::json& root) {
		if (!root.contains ("version") || !root.contains ("fields")) {
			return false;
		}

		const auto& fields_obj = root ["fields"];
		if (!fields_obj.is_object ()) {
			return false;
		}

		auto& reg = detail::get_registry ();

		std::unordered_map<std::uint32_t, field*> lookup;
		lookup.reserve (reg.fields.size ());

		for (auto& f : reg.fields) {
			lookup [f.key] = &f;
		}

		for (auto it = fields_obj.begin (); it != fields_obj.end (); ++it) {
			auto found = lookup.find (static_cast<std::uint32_t>(std::strtoul (it.key ().c_str (), nullptr, 16)));
			if (found == lookup.end ()) {
				continue;
			}

			serial::json_to_field (it.value (), *found->second);
		}
		return true;
	}

	inline nlohmann::json to_json_delta () {
		auto& reg = detail::get_registry ();
		auto fields_obj = nlohmann::json::object ();

		for (const auto& f : reg.fields) {
			char key_str [12];
			std::snprintf (key_str, sizeof (key_str), "%08x", f.key);

			auto current = serial::field_to_json (f);
			auto it = reg.defaults.find (key_str);

			if (it != reg.defaults.end () && *it == current) {
				continue;
			}

			fields_obj [key_str] = std::move (current);
		}

		return nlohmann::json {{ "v", k_version }, { "f", std::move (fields_obj) }};
	}

	inline bool from_json_delta (const nlohmann::json& root) {
		if (!root.contains ("f")) {
			return false;
		}

		const auto& fields_obj = root ["f"];
		if (!fields_obj.is_object ()) {
			return false;
		}

		auto& reg = detail::get_registry ();

		const auto delta_ver = root.value ("v", 1);
		const nlohmann::json* baseline = &reg.defaults;
		if (delta_ver < k_delta_blank_baseline_version && reg.factory_defaults.is_object () && !reg.factory_defaults.empty ()) {
			baseline = &reg.factory_defaults;
		}

		std::unordered_map<std::uint32_t, field*> lookup;
		lookup.reserve (reg.fields.size ());

		for (auto& f : reg.fields) {
			lookup [f.key] = &f;

			char key_str [12];
			std::snprintf (key_str, sizeof (key_str), "%08x", f.key);

			auto def = baseline->find (key_str);
			if (def != baseline->end ()) {
				serial::json_to_field (*def, f);
			}
		}

		for (auto it = fields_obj.begin (); it != fields_obj.end (); ++it) {
			auto found = lookup.find (static_cast<std::uint32_t>(std::strtoul (it.key ().c_str (), nullptr, 16)));
			if (found == lookup.end ()) {
				continue;
			}

			serial::json_to_field (it.value (), *found->second);
		}

		return true;
	}

	namespace registry {

		inline constexpr wchar_t k_root_key [] {L"Software\\DarkFox\\configs"};

		inline bool save (std::wstring_view name) {
			HKEY hkey {};
			if (RegCreateKeyExW (HKEY_CURRENT_USER, k_root_key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
				return false;
			}

			const auto json_str = to_json ().dump (-1);
			const auto compressed = compress::deflate (json_str);
			const auto result = RegSetValueExW (hkey, name.data (), 0, REG_BINARY, compressed.data (), static_cast<DWORD>(compressed.size ()));

			RegCloseKey (hkey);
			return result == ERROR_SUCCESS;
		}

		inline bool load (std::wstring_view name) {
			HKEY hkey {};
			if (RegOpenKeyExW (HKEY_CURRENT_USER, k_root_key, 0, KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS) {
				return false;
			}

			DWORD size {};
			if (RegQueryValueExW (hkey, name.data (), nullptr, nullptr, nullptr, &size) != ERROR_SUCCESS || size == 0) {
				RegCloseKey (hkey);
				return false;
			}

			std::vector<std::uint8_t> buf (size);

			if (RegQueryValueExW (hkey, name.data (), nullptr, nullptr, buf.data (), &size) != ERROR_SUCCESS) {
				RegCloseKey (hkey);
				return false;
			}

			RegCloseKey (hkey);

			const auto json_str = compress::inflate (buf.data (), buf.size ());
			if (!json_str) {
				return false;
			}

			try {
				const auto j = nlohmann::json::parse (*json_str, nullptr, false);
				if (j.is_discarded ()) {
					return false;
				}

				// Сброс всех полей на дефолты ПЕРЕД загрузкой
				auto& reg = detail::get_registry ();
				for (auto& f : reg.fields) {
					char key_str [12];
					std::snprintf (key_str, sizeof (key_str), "%08x", f.key);
					auto def = reg.defaults.find (key_str);
					if (def != reg.defaults.end ())
						serial::json_to_field (*def, f);
				}

				return from_json (j);
			} 				catch (...) {
 return false; }
				}

				inline bool remove (std::wstring_view name) {
					HKEY hkey {};
					if (RegOpenKeyExW (HKEY_CURRENT_USER, k_root_key, 0, KEY_SET_VALUE, &hkey) != ERROR_SUCCESS) {
						return false;
					}

					const auto result = RegDeleteValueW (hkey, name.data ());
					RegCloseKey (hkey);
					return result == ERROR_SUCCESS;
				}

				inline std::vector<std::wstring> list () {
					std::vector<std::wstring> names;

					HKEY hkey {};
					if (RegOpenKeyExW (HKEY_CURRENT_USER, k_root_key, 0, KEY_QUERY_VALUE, &hkey) != ERROR_SUCCESS) {
						return names;
					}

					wchar_t name_buf [256] {};
					DWORD index {};

					while (true) {
						DWORD name_len {256};
						if (RegEnumValueW (hkey, index++, name_buf, &name_len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
							break;
						}

						names.emplace_back (name_buf, name_len);
					}

					RegCloseKey (hkey);
					std::sort (names.begin (), names.end ());
					return names;
				}

			} // namespace registry

			// ============================================================
			// NEW: File-based config storage (%APPDATA%\DarkFox\configs\)
			// ============================================================
			namespace files {

				inline std::wstring get_configs_directory () {
					wchar_t app_data [MAX_PATH] {};
					if (FAILED (SHGetFolderPathW (nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, app_data))) {
						return {};
					}

					const auto root = std::wstring (app_data) + L"\\DarkFox";
					const auto configs = root + L"\\configs";

					CreateDirectoryW (root.c_str (), nullptr);
					CreateDirectoryW (configs.c_str (), nullptr);

					return configs;
				}

				inline bool save (std::wstring_view name) {
					const auto dir = get_configs_directory ();
					if (dir.empty () || name.empty ()) {
						return false;
					}

					// Sanitize filename - только буквы, цифры, _, -, пробелы
					std::wstring clean_name;
					clean_name.reserve (name.size ());
					for (auto c : name) {
						if (iswalnum (c) || c == L'_' || c == L'-' || c == L' ') {
							clean_name.push_back (c);
						}
					}

					if (clean_name.empty ()) {
						return false;
					}

					const auto path = dir + L"\\" + clean_name + L".cfg";

					// JSON в pretty-print формате (с отступами) - можно редактировать вручную
					const auto json_str = to_json ().dump (4);

					const auto handle = CreateFileW (
						path.c_str (), GENERIC_WRITE, 0, nullptr,
						CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
					);
					if (handle == INVALID_HANDLE_VALUE) {
						return false;
					}

					DWORD written {};
					const auto ok = WriteFile (
						handle, json_str.data (),
						static_cast<DWORD>(json_str.size ()), &written, nullptr
					);
					CloseHandle (handle);

					return ok && written == json_str.size ();
				}

				inline bool load (std::wstring_view name) {
					const auto dir = get_configs_directory ();
					if (dir.empty () || name.empty ()) {
						return false;
					}

					const auto path = dir + L"\\" + std::wstring (name) + L".cfg";

					const auto handle = CreateFileW (
						path.c_str (), GENERIC_READ, FILE_SHARE_READ, nullptr,
						OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
					);
					if (handle == INVALID_HANDLE_VALUE) {
						return false;
					}

					LARGE_INTEGER size {};
					if (!GetFileSizeEx (handle, &size) || size.QuadPart <= 0 || size.QuadPart > 64 * 1024 * 1024) {
						CloseHandle (handle);
						return false;
					}

					std::string content;
					content.resize (static_cast<std::size_t>(size.QuadPart));

					DWORD read {};
					const auto ok = ReadFile (
						handle, content.data (),
						static_cast<DWORD>(content.size ()), &read, nullptr
					);
					CloseHandle (handle);

					if (!ok || read != content.size ()) {
						return false;
					}

					try {
						const auto j = nlohmann::json::parse (content, nullptr, false);
						if (j.is_discarded ()) {
							return false;
						}

						// Reset all fields to defaults BEFORE loading (как в registry::load)
						auto& reg = detail::get_registry ();
						for (auto& f : reg.fields) {
							char key_str [12];
							std::snprintf (key_str, sizeof (key_str), "%08x", f.key);
							auto def = reg.defaults.find (key_str);
							if (def != reg.defaults.end ()) {
								serial::json_to_field (*def, f);
							}
						}

						return from_json (j);
					} 						catch (...) {
 return false; }
						}

						inline bool remove (std::wstring_view name) {
							const auto dir = get_configs_directory ();
							if (dir.empty () || name.empty ()) {
								return false;
							}

							const auto path = dir + L"\\" + std::wstring (name) + L".cfg";
							return DeleteFileW (path.c_str ()) != 0;
						}

						inline std::vector<std::wstring> list () {
							std::vector<std::wstring> names;

							const auto dir = get_configs_directory ();
							if (dir.empty ()) {
								return names;
							}

							const auto search = dir + L"\\*.cfg";

							WIN32_FIND_DATAW data {};
							const auto handle = FindFirstFileW (search.c_str (), &data);
							if (handle == INVALID_HANDLE_VALUE) {
								return names;
							}

							do {
								if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
									continue;
								}

								std::wstring filename (data.cFileName);

								// Remove .cfg extension
								if (filename.size () > 4 && filename.substr (filename.size () - 4) == L".cfg") {
									filename.resize (filename.size () - 4);
								}

								names.emplace_back (std::move (filename));
							} while (FindNextFileW (handle, &data));

							FindClose (handle);
							std::sort (names.begin (), names.end ());
							return names;
						}

						inline void open_directory () {
							const auto dir = get_configs_directory ();
							if (dir.empty ()) {
								return;
							}

							ShellExecuteW (nullptr, L"open", dir.c_str (), nullptr, nullptr, SW_SHOW);
						}

					} // namespace files

					namespace share_detail {

						inline std::vector<std::uint8_t> make_payload (std::string_view name = {}) {
							auto delta = to_json_delta ();

							if (!name.empty ()) {
								delta ["n"] = std::string (name);
							}

							const auto json_str = delta.dump (-1);
							const auto compressed = compress::deflate (json_str);

							if (compressed.empty ()) {
								return {};
							}

							std::vector<std::uint8_t> payload;
							payload.reserve (3 + compressed.size ());
							payload.push_back (k_share_magic [0]);
							payload.push_back (k_share_magic [1]);
							payload.push_back (static_cast<std::uint8_t>(k_version));
							payload.insert (payload.end (), compressed.begin (), compressed.end ());
							return payload;
						}

						struct import_result {
							bool success {};
							std::string name {};
						};

						inline import_result import_payload (const std::vector<std::uint8_t>& d) {
							if (d.size () < 4) {
								return {};
							}

							if (d [0] != k_share_magic [0] || d [1] != k_share_magic [1]) {
								return {};
							}

							const auto json_str = compress::inflate (d.data () + 3, d.size () - 3);
							if (!json_str) {
								return {};
							}

							try {
								const auto j = nlohmann::json::parse (*json_str, nullptr, false);
								if (j.is_discarded ()) {
									return {};
								}

								std::string name {};
								if (j.contains ("n") && j ["n"].is_string ()) {
									name = j ["n"].get<std::string> ();
								}

								bool ok {};
								if (j.contains ("f")) {
									ok = from_json_delta (j);
								} else {
									ok = from_json (j);
								}

								return {ok, std::move (name)};
							} 								catch (...) {
 return {}; }
								}

							} // namespace share_detail

							inline std::string export_share () {
								const auto payload = share_detail::make_payload ();
								if (payload.empty ()) {
									return {};
								}

								return base64::encode (payload.data (), payload.size ());
							}

							inline std::string export_share_words (std::string_view name = {}) {
								const auto payload = share_detail::make_payload (name);
								if (payload.empty ()) {
									return {};
								}

								return words::encode (payload.data (), payload.size ());
							}

							inline std::string export_share (std::string_view name = {}) {
								const auto payload = share_detail::make_payload (name);
								if (payload.empty ()) {
									return {};
								}

								return base64::encode (payload.data (), payload.size ());
							}

							inline share_detail::import_result import_share (std::string_view share_str) {
								std::string cleaned;
								cleaned.reserve (share_str.size ());

								for (auto c : share_str) {
									if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
										cleaned.push_back (c);
									}
								}

								const auto decoded = base64::decode (cleaned);
								if (!decoded) {
									return {};
								}

								return share_detail::import_payload (*decoded);
							}

							inline share_detail::import_result import_share_words (std::string_view word_str) {
								const auto decoded = words::decode (word_str);
								if (!decoded) {
									return {};
								}

								return share_detail::import_payload (*decoded);
							}

							inline share_detail::import_result import_auto (std::string_view input) {
								while (!input.empty () && (input.front () == ' ' || input.front () == '\n' || input.front () == '\r')) {
									input.remove_prefix (1);
								}

								while (!input.empty () && (input.back () == ' ' || input.back () == '\n' || input.back () == '\r')) {
									input.remove_suffix (1);
								}

								if (input.empty ()) {
									return {};
								}

							if (input.find ('|') != std::string_view::npos) {
								return import_share_words (input);
							}

							return import_share (input);
						}

						// ============================================================
						// Shipped profile
						//
						// The profile the cheat boots with on a machine that has no
						// saved config of its own. It is a full `to_json()` document
						// (version 2, keyed by field hash) embedded as a string literal
						// rather than a compiled-in table, so it stays a plain config
						// file: regenerate it with the export button, paste the JSON
						// back here, rebuild.
						//
						// Ordering contract -- entry.cpp calls these in the order
						// initialize(), install(), apply_blank_profile():
						//   * initialize() snapshots the profile string into the
						//     registry while every field still holds its compile-time
						//     value, so factory_defaults is never polluted;
						//   * install() writes the profile into the live fields, which
						//     makes serialize() reproduce exactly it;
						//   * apply_blank_profile() overwrites reg.defaults with the
						//     profile, so a later "reset" and a fresh `files::load`
						//     both land on the shipped state instead of on zeroes.
						// ============================================================
						// Seed the shipped profile from the literal in
						// core/defaults.hpp. Called once at boot, before
						// apply_blank_profile(), while every field still holds its
						// compile-time value -- see the ordering note in entry.cpp.
						// The literal is not included here: this file lives in
						// external/ and should not reach into core/.
						inline void capture_default_profile ( const char* json ) {
							if ( json && *json ) {
								defaults::get ( ) = json;
							}
						}

						// Applies the shipped profile to the live fields. Returns false
						// when nothing was captured (caller then falls back to its own
						// defaults).
						inline bool install_default_profile ( ) {
							const auto& doc = defaults::parsed ( );
							if ( !doc.is_object ( ) || !doc.contains ( "fields" ) ) {
								return false;
							}

							return from_json ( doc );
						}

						// Pretty-printed copy of the *current* state, for the "save as
						// default" button. What it writes is what gen_defaults.py turns
						// back into the literal.
						inline std::string export_default_profile ( ) {
							return to_json ( ).dump ( 4 );
						}

					} // namespace config