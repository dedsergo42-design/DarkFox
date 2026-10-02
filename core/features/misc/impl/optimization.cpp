#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

#include "../misc.hpp"

namespace features::misc {

	namespace {

		// Имена материалов, по которым опознаётся объект. Список намеренно
		// строится на подстроках, а не на точных путях: Valve переименовывает
		// ассеты между операциями, а префикс папки (`materials/foliage/`,
		// `materials/particle/`) живёт годами и остаётся узнаваемым.
		//
		// Классификация идёт по ПЕРВОМУ совпадению в порядке таблицы, поэтому
		// более специфичные правила обязаны стоять выше общих. Пример: трава --
		// тоже листва, но у неё свой тумблер, значит `foliage/grass` обязан
		// проверяться раньше, чем `foliage/`.
		struct prefix_rule
		{
			std::string_view needle;
			optimization::category kind;
		};

		constexpr std::array k_rules
		{
			// --- трава (до общей листвы) ---
			prefix_rule{ "grass", optimization::category::grass },
			prefix_rule{ "foliage/grass", optimization::category::grass },

			// --- листва и деревья ---
			prefix_rule{ "foliage/", optimization::category::foliage },
			prefix_rule{ "de_/trees", optimization::category::foliage },
			prefix_rule{ "trees/", optimization::category::foliage },
			prefix_rule{ "tree_", optimization::category::foliage },
			prefix_rule{ "bush", optimization::category::foliage },
			prefix_rule{ "hedge", optimization::category::foliage },
			prefix_rule{ "fern", optimization::category::foliage },
			prefix_rule{ "ivy", optimization::category::foliage },

			// --- тросы, провода, верёвки ---
			prefix_rule{ "rope", optimization::category::rope },
			prefix_rule{ "wire", optimization::category::rope },
			prefix_rule{ "cable", optimization::category::rope },
			prefix_rule{ "chainlink", optimization::category::rope },

			// --- погодные частицы (до общих частиц) ---
			prefix_rule{ "rain", optimization::category::weather },
			prefix_rule{ "snow", optimization::category::weather },
			prefix_rule{ "weather", optimization::category::weather },
			prefix_rule{ "fog_volume", optimization::category::weather },

			// --- частицы ---
			prefix_rule{ "particle/", optimization::category::particle },
			prefix_rule{ "particles/", optimization::category::particle },
			prefix_rule{ "smoke", optimization::category::particle },
			prefix_rule{ "dust_mote", optimization::category::particle },
			prefix_rule{ "spark", optimization::category::particle },
			prefix_rule{ "debris", optimization::category::particle },

			// --- лучи и трассеры ---
			prefix_rule{ "beam", optimization::category::beam },
			prefix_rule{ "tracer", optimization::category::beam },
			prefix_rule{ "laser", optimization::category::beam },

			// --- глоу-спрайты ---
			prefix_rule{ "glow", optimization::category::glow_sprite },
			prefix_rule{ "halo", optimization::category::glow_sprite },
			prefix_rule{ "sprite", optimization::category::glow_sprite },

			// --- пропа ---
			prefix_rule{ "props/", optimization::category::prop },
			prefix_rule{ "models/props", optimization::category::prop },
			prefix_rule{ "barrel", optimization::category::prop },
			prefix_rule{ "crate", optimization::category::prop },

			// --- декали ---
			prefix_rule{ "decal", optimization::category::decal },
			prefix_rule{ "overlay/", optimization::category::decal },
		};

		// Материалы, которые НИКОГДА не трогаем, даже если правило совпало.
		// Это то, на что смотрит игра и игрок: без него картинка ломается, а
		// иногда и геймплей (стекло, вода, дым-завеса -- элементы карты).
		constexpr std::array k_never_touch
		{
			"tools/toolsblack",
			"tools/toolsclip",
			"tools/toolsskybox",
			"tools/toolsinvisible",
			"dev/",
			"editor/",
			"player/",
			"models/player",
			"weapons/",
			"models/weapons",
			"glass",
			"water",
		};

	} // namespace

	void optimization::on_level_change( )
	{
		for ( auto& slot : this->m_object_cache )
		{
			slot.key.store( 0, std::memory_order_relaxed );
			slot.value.store( 0, std::memory_order_relaxed );
			slot.frame.store( 0, std::memory_order_relaxed );
		}

		for ( auto& slot : this->m_cardboard_cache )
		{
			slot.key = 0;
			slot.value = 0;
		}

		this->m_frame = 0;
		this->m_flat_material = 0;
		this->m_flat_signature = 0;
		this->m_flat_ready = false;
		this->m_cardboard_signature = 0;
		this->m_culled_objects = 0;
		this->m_seen_objects = 0;
	}

	void optimization::on_frame_stage_notify( )
	{
		// Счётчики кадра валим ЗДЕСЬ, на входе в новый кадр: on_draw_scene_object
		// вызывается уже внутри кадра и должен видеть накопленное за него.
		// Проверка на нулевой кадр нужна, чтобы первый кадр не потерял данные
		// предыдущего -- после on_level_change счётчики и так нули, но порядок
		// вызовов между level_change и frame_stage_notify не гарантирован.
		if ( this->m_frame != 0 )
		{
			this->m_seen_objects = 0;
			this->m_culled_objects = 0;
		}

		++this->m_frame;
	}

	// Имя материала приходит как NUL-терминированная строка. Длину ограничиваем
	// на входе в std::string_view: битый материал может отдать указатель на
	// большой буфер, и без границы поиск подстроки уйдёт читать лишнее.
	std::string_view optimization::normalize( std::string_view name )
	{
		const auto end = std::min< std::size_t >( name.size( ), 256 );
		return name.substr( 0, end );
	}

	optimization::category optimization::classify( std::string_view name )
	{
		if ( name.empty( ) )
		{
			return category::none;
		}

		for ( const auto& banned : k_never_touch )
		{
			if ( name.find( banned ) != std::string_view::npos )
			{
				return category::none;
			}
		}

		for ( const auto& rule : k_rules )
		{
			if ( name.find( rule.needle ) != std::string_view::npos )
			{
				return rule.kind;
			}
		}

		return category::none;
	}

	optimization::category optimization::classify_material( std::uintptr_t material )
	{
		if ( !material )
		{
			return category::none;
		}

		const auto key = static_cast< std::uint32_t >( material >> 4 );

		// Индекс -- хеш ключа, а не голый key % slots.
		//
		// На материальном наборе CS2 выигрыш от хеша невелик: промахи здесь
		// упираются в ЁМКОСТЬ кэша, а не в распределение (замер и подбор
		// размера -- в optimization.hpp). Но умножение на нечётную константу
		// стоит один такт и снимает зависимость от младших бит адреса:
		// материалы выровнены на 16 байт, а аллокатор раздаёт их с шагом в
		// десятки килобайт, то есть различие живёт ВЫШЕ 16-го бита -- и
		// остаток по степени двойки его не видел бы вовсе. Константа --
		// золотое сечение в 32 битах, обычный фибоначчиев хеш.
		constexpr auto k_hash_mul{ 0x9E3779B9u };

		const auto index = ( key * k_hash_mul ) % k_object_cache_slots;
		auto& slot = this->m_object_cache[ index ];

		if ( slot.key.load( std::memory_order_relaxed ) == key )
		{
			slot.frame.store( this->m_frame, std::memory_order_relaxed );
			return static_cast< category >( slot.value.load( std::memory_order_relaxed ) );
		}

		// Имя материала достаётся через vtable-0. Это единственный вызов в
		// горячем пути, поэтому он и вынесен под кэш выше. call_vfunc уже
		// сам валидирует vtable-слот, так что падения на «не том» типе
		// материала здесь не будет.
		const auto raw = memory::call_vfunc<const char*>( material, 0 );
		const auto kind = raw
			? classify( normalize( std::string_view{ raw } ) )
			: category::none;

		slot.key.store( key, std::memory_order_relaxed );
		slot.value.store( static_cast< std::uintptr_t >( kind ), std::memory_order_relaxed );
		slot.frame.store( this->m_frame, std::memory_order_relaxed );

		return kind;
	}

	bool optimization::should_cull( category kind ) const
	{
		const auto& opt = settings::g_misc.m_optimization;

		switch ( kind )
		{
		case category::grass:       return opt.remove_grass.value;
		case category::foliage:     return opt.remove_foliage.value;
		case category::rope:        return opt.remove_ropes.value;
		case category::particle:    return opt.remove_particles.value;
		case category::beam:        return opt.remove_beams.value;
		case category::glow_sprite: return opt.remove_glow.value;
		case category::weather:     return opt.remove_rain_snow.value;
		case category::decal:       return opt.remove_decals.value;

		// Пропа не решается категорией: она проходит через порог размера,
		// см. on_draw_scene_object. Здесь отвечаем «нет», иначе мелкий проп
		// вырезался бы дважды, а крупный -- мимо порога.
		case category::prop:        return false;
		default:                    return false;
		}
	}

	// Модель игрока опознаётся по своему префиксу пути. Проверка отделена от
	// classify, потому что материал игрока обязан остаться в k_never_touch --
	// вырезать противника нельзя ни при каких настройках.
	//
	// Кэш свой, а не общий с classify: иначе пришлось бы расширять
	// category двумя дополнительными значениями (player_world/player_skin),
	// а таблица правил в .cpp и так держится на совпадении индексов.
	bool optimization::is_player_material( std::uintptr_t material )
	{
		if ( !material )
		{
			return false;
		}

		constexpr std::array needles
		{
			"models/player",
			"player/",
		};

		const auto raw = memory::call_vfunc<const char*>( material, 0 );
		if ( !raw )
		{
			return false;
		}

		const auto name = normalize( std::string_view{ raw } );

		for ( const auto& needle : needles )
		{
			if ( name.find( needle ) != std::string_view::npos )
			{
				return true;
			}
		}

		return false;
	}

	// Плоский материал: тот же шейдер, что у оригинала, но с выключенным
	// освещением и без карт нормалей. Геометрия остаётся, поэтому объект
	// по-прежнему пишет глубину и перекрывает то, что за ним -- то есть не
	// ломает видимость уровня, только его затенение.
	//
	// Флаги снятия слоёв берутся из настроек и запекаются в KV один раз:
	// материал неизменяем после создания, так что пересборка KV на каждое
	// движение ползунка бессмысленна -- смену набора ловит m_flat_signature.
	std::uintptr_t optimization::build_flat_material( )
	{
		const auto& opt = settings::g_misc.m_optimization;
		const auto& color = opt.light_color.value;

		// Базовый KV. Слои добавляются условно: пустая строка вместо пути
		// ресурса -- штатный способ сказать Source 2 «этого слоя нет».
		std::string kv = std::format(
			"<!-- kv3 encoding:text:version{{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}} "
			"format:generic:version{{7412167c-06e9-4698-aff2-e63eb59037e7}} -->\n"
			"{{\n"
			"    shader = \"csgo_complex.vfx\"\n"
			"    F_ALPHA_TEST = 0\n"
			"    F_TRANSLUCENT = 0\n"
			"    F_NORMAL_MAP = {}\n"
			"    F_SPECULAR = {}\n"
			"    g_vColorTint = [{}, {}, {}, 1.0]\n"
			"    g_flSelfIllumScale = 1.0\n"
			"    g_flSelfIllumFresnel = 0.0\n",
			opt.remove_normal_maps.value ? 0 : 1,
			opt.remove_specular.value ? 0 : 1,
			static_cast<float>( color.r ) / 255.0f,
			static_cast<float>( color.g ) / 255.0f,
			static_cast<float>( color.b ) / 255.0f );

		if ( opt.remove_normal_maps.value )
		{
			// Нормали не читаются шейдером -- экономим и выборку, и память
			// под текстуру: пустой путь ресурса означает «слоя нет».
			kv += "    TextureNormal = \"\"\n";
			kv += "    g_tNormalTexture = \"\"\n";
			kv += "    g_tNormal = \"\"\n";
			kv += "    g_flNormalScale = 0.0\n";
		}

		if ( opt.remove_specular.value )
		{
			kv += "    g_flMetalness = 0.0\n";
			kv += "    g_flRoughness = 1.0\n";
			kv += "    g_tAmbientOcclusion = \"\"\n";
		}

		if ( opt.remove_detail.value )
		{
			// Детальный слой -- это вторая выборка альбедо на пиксель, самый
			// дорогой из трёх снимаемых слоёв.
			kv += "    g_tDetailTexture = \"\"\n";
			kv += "    g_flDetailScale = 0.0\n";
			kv += "    g_flDetailBlendFactor = 0.0\n";
		}

		if ( opt.remove_shadows.value )
		{
			// F_CAST_SHADOWS снимается на уровне материала: объект перестаёт
			// попадать в shadow-атлас, и проход теней для него не считается.
			kv += "    F_CAST_SHADOWS = 0\n";
			kv += "    g_bReceiveShadows = 0\n";
		}

		kv += "}\n";

		const auto name = std::format( "DarkFox_opt_flat_{:08x}",
			fnv1a::runtime_hash( kv.c_str( ) ) );

		return systems::materials::load( kv.c_str( ), name.c_str( ) );
	}

	std::uintptr_t optimization::prefixed_material( )
	{
		const auto& opt = settings::g_misc.m_optimization;

		const auto signature =
			( opt.remove_normal_maps.value ? 1u : 0u ) |
			( opt.remove_specular.value ? 2u : 0u ) |
			( opt.remove_detail.value ? 4u : 0u ) |
			( opt.remove_shadows.value ? 8u : 0u );

		if ( this->m_flat_ready && signature == this->m_flat_signature )
		{
			return this->m_flat_material;
		}

		this->m_flat_ready = true;
		this->m_flat_signature = signature;
		this->m_flat_material = this->build_flat_material( );

		if ( this->m_flat_material )
		{
			logging::console::print( "[optimization] flat material ready at 0x{:x} (layers {:x})",
				this->m_flat_material, signature );
		}

		return this->m_flat_material;
	}

	// Картонная текстура.
	//
	// Это не «упрощение» родного материала, а его замена: альбедо становится
	// одной заливкой, поверх которой работает штатный шейдер. Отсюда
	// требования к результату:
	//
	//   * поверхность обязана остаться освещённой, иначе сцена превратится в
	//     силуэты и пропадёт объём -- поэтому шейдер csgo_complex.vfx
	//     сохраняется, а g_tColor указывает на белую заглушку, которую
	//     умножает g_vColorTint;
	//
	//   * все остальные выборки снимаются -- это и есть выигрыш: вместо
	//     альбедо + нормалей + AO + маски бликов + детального слоя на пиксель
	//     остаётся одна выборка константы;
	//
	//   * шероховатость выкручена в максимум, металличность в ноль. Картон
	//     матовый, а матовость -- это ещё и отказ от расчёта блика по
	//     полному BRDF.
	std::uintptr_t optimization::build_cardboard_material( std::uintptr_t source, bool player_skin )
	{
		const auto& opt = settings::g_misc.m_optimization;
		const auto& color = opt.cardboard_color.value;
		const auto rough = std::clamp( opt.cardboard_roughness.value, 0.0f, 1.0f );

		// Только r/g/b: у model-материалов четвёртый компонент это непрозрачность,
		// и передача alpha сюда сделала бы игроков полупрозрачными.
		const auto r = static_cast<float>( color.r ) / 255.0f;
		const auto g = static_cast<float>( color.g ) / 255.0f;
		const auto b = static_cast<float>( color.b ) / 255.0f;

		// Белая заглушка альбедо. Именно тот же ресурс, которым уже пользуются
		// cham-материалы (materials.cpp), поэтому путь гарантированно валиден
		// и лежит в pak -- не надо тащить свою текстуру.
		constexpr auto white = "materials/dev/primary_white_color_tga_21186c76.vtex";
		constexpr auto black = "materials/dev/primary_black_color_tga_21186c76.vtex";
		constexpr auto flat_normal = "materials/default/default_normal_tga_7652cb.vtex";
		constexpr auto flat_mask = "materials/default/default_mask_tga_fde710a5.vtex";

		std::string kv;

		if ( player_skin )
		{
			// Игроки идут через csgo_character.vfx, но в остальном это тот же
			// картон. F_CLOTH_SHADING даёт одинаково матовую поверхность на
			// всех позах, что для однотонного силуэта только в плюс.
			kv = std::format(
				"<!-- kv3 encoding:text:version{{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}} "
				"format:generic:version{{7412167c-06e9-4698-aff2-e63eb59037e7}} -->\n"
				"{{\n"
				"    shader = \"csgo_character.vfx\"\n"
				"    F_NORMAL_MAP = 0\n"
				"    F_SPECULAR = 0\n"
				"    F_DETAIL = 0\n"
				"    F_CLOTH_SHADING = 1\n"
				"    F_CAST_SHADOWS = 0\n"
				"    g_vColorTint = [{}, {}, {}, 1.0]\n"
				"    g_flModelTintAmount = 1.0\n"
				"    g_flRoughness = 1.0\n"
				"    g_flMetalness = 0.0\n"
				"    g_tColor = resource:\"{}\"\n"
				"    g_tNormal = resource:\"{}\"\n"
				"    g_tAmbientOcclusion = resource:\"{}\"\n"
				"    g_tTintMask = resource:\"{}\"\n"
				"}}\n",
				r, g, b, white, flat_normal, flat_mask, flat_mask );
		}
		else
		{
			// Мир. resources:"..." -- статический ресурс, а не динамическая
			// строка; Source 2 по этому префиксу резолвит путь на этапе
			// загрузки материала и не перезапрашивает его каждый кадр.
			kv = std::format(
				"<!-- kv3 encoding:text:version{{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d}} "
				"format:generic:version{{7412167c-06e9-4698-aff2-e63eb59037e7}} -->\n"
				"{{\n"
				"    shader = \"csgo_complex.vfx\"\n"
				"    F_NORMAL_MAP = 0\n"
				"    F_SPECULAR = 0\n"
				"    F_DETAIL = 0\n"
				"    F_ALPHA_TEST = 0\n"
				"    F_TRANSLUCENT = 0\n"
				"    F_CAST_SHADOWS = 0\n"
				"    g_vColorTint = [{}, {}, {}, 1.0]\n"
				"    g_flRoughness = {}\n"
				"    g_flMetalness = 0.0\n"
				"    g_tColor = resource:\"{}\"\n"
				"    g_tNormal = resource:\"{}\"\n"
				"    g_tAmbientOcclusion = resource:\"{}\"\n"
				// Самосвет гасится парой значений, а не одним: множитель в
				// ноль делает поверхность целиком зависимой от света сцены --
				// иначе картон светился бы в темноте собственным albedo, и
				// силуэты игроков выглядели бы ярче стен.
				"    g_flSelfIllumScale = 0.0\n"
				"    g_vSelfIllumTint = [0.0, 0.0, 0.0]\n"
				// Блик по маске: чёрная маска означает «блика нет» без снятия
				// самого F_SPECULAR, что нужно для материалов, где флаг
				// выставляется движком поверх нашего KV.
				"    g_tTintMask = resource:\"{}\"\n"
				"    g_tSelfIllumMask = resource:\"{}\"\n"
				"}}\n",
				r, g, b, rough, white, flat_normal, flat_mask, flat_mask, black );
		}

		const auto name = std::format( "DarkFox_cardboard_{:08x}_{:x}",
			fnv1a::runtime_hash( kv.c_str( ) ), source );

		// source в имени, а не только в хеше: два разных исходных материала с
		// одинаковым KV всё равно должны получить разные имена, иначе
		// material_create вернёт уже существующий материал чужого объекта.
		const auto made = systems::materials::load( kv.c_str( ), name.c_str( ) );

		if ( made )
		{
			logging::console::print( "[optimization] cardboard material 0x{:x} for source 0x{:x}",
				made, source );
		}

		return made;
	}

	std::uintptr_t optimization::cardboard_material( std::uintptr_t source, bool player_skin )
	{
		const auto& opt = settings::g_misc.m_optimization;
		const auto& color = opt.cardboard_color.value;

		// Сигнатура = всё, что запечено в KV. Меняется цвет или
		// шероховатость -- меняется ключ, и материал пересоздаётся; иначе
		// ползунок цвета не давал бы эффекта до перезахода на уровень.
		const auto signature =
			( static_cast< std::uint32_t >( color.r ) << 24 ) |
			( static_cast< std::uint32_t >( color.g ) << 16 ) |
			( static_cast< std::uint32_t >( color.b ) << 8 ) |
			static_cast< std::uint32_t >( opt.cardboard_roughness.value * 255.0f );

		if ( signature != this->m_cardboard_signature )
		{
			for ( auto& slot : this->m_cardboard_cache )
			{
				slot.key = 0;
				slot.value = 0;
			}
			this->m_cardboard_signature = signature;
		}

		// Признак «игрок» входит в ключ: у игроков другой шейдер и другой KV,
		// и общий слот отдал бы мировой материал модели игрока.
		const auto key = static_cast< std::uint64_t >( source )
			^ ( static_cast< std::uint64_t >( signature ) << 40 )
			^ ( player_skin ? 0x8000'0000'0000'0000ull : 0ull );

		auto& slot = this->m_cardboard_cache[ key % k_cardboard_cache_slots ];
		if ( slot.key == key && slot.value )
		{
			return slot.value;
		}

		const auto made = this->build_cardboard_material( source, player_skin );

		slot.key = key;
		slot.value = made;

		return made;
	}

	bool optimization::should_skip_object( std::uintptr_t material )
	{
		if ( !this->active( ) || !material )
		{
			return false;
		}

		const auto& opt = settings::g_misc.m_optimization;

		// Тени гасятся на живом материале, а не в KV. Причина в том, что
		// картон и flat строятся только когда включены соответствующие
		// режимы, а `remove shadows` обязан работать сам по себе -- иначе
		// тумблер молча ничего не делал бы при выключенном картоне.
		if ( opt.remove_shadows.value )
		{
			this->strip_shadows( material );
		}

		const auto kind = this->classify_material( material );

		++this->m_seen_objects;

		if ( this->should_cull( kind ) )
		{
			++this->m_culled_objects;
			return true;
		}

		return false;
	}

	// Снятие теней с уже загруженного материала.
	//
	// PrepareSceneMaterial вызывается один раз на материал при загрузке
	// уровня, поэтому запись переживает весь матч и не повторяется каждый
	// кадр. Параметры лежат массивом по 0x40 байт: имя по +0x28, значение по
	// +0x00 -- та же раскладка, на которую опирается removals.cpp.
	//
	// Кроме флагов на самом материале гасится и `g_flShadowFadeDistance`:
	// именно он задаёт, на какой дистанции объект перестаёт отбрасывать
	// тень. При нулевом значении объект вылетает из shadow-атласа сразу,
	// даже если флаг каста движок выставил поверх нашего KV.
	void optimization::strip_shadows( std::uintptr_t material ) const
	{
		const auto parameters = memory::safe_read<std::uintptr_t>( material + 0x20 );
		const auto count = memory::safe_read<std::uint32_t>( material + 0x18 );

		if ( !parameters || !count || *count > 4096 )
		{
			return;
		}

		const auto base = *parameters;

		for ( auto i = 0u; i < *count; ++i )
		{
			const auto parameter = base + ( static_cast< std::size_t >( i ) * 0x40 );
			const auto name = memory::safe_read<const char*>( parameter + 0x28 );

			if ( !name || !*name )
			{
				continue;
			}

			const auto hash = fnv1a::runtime_hash( *name );

			if ( hash == "g_flShadowFadeDistance"_hash
				|| hash == "g_flShadowStrength"_hash
				|| hash == "g_flShadowBlur"_hash )
			{
				memory::safe_write<float>( parameter, 0.0f );
				continue;
			}

			if ( hash == "F_CAST_SHADOWS"_hash || hash == "g_bReceiveShadows"_hash )
			{
				memory::safe_write<int>( parameter, 0 );
			}
		}
	}

	void optimization::on_draw_scene_object( std::uintptr_t batch, int batch_count )
	{
		if ( !this->active( ) || !batch || batch_count <= 0 || batch_count > ( 1 << 20 ) )
		{
			return;
		}

		const auto& opt = settings::g_misc.m_optimization;

		// Всё, что решается до входа в цикл, считается здесь: на батч из
		// нескольких тысяч примитивов разница между чтением флага в цикле и
		// снаружи -- это уже заметная доля кадра.
		const auto wants_flat = opt.flat_lighting.value;
		const auto wants_cardboard = opt.cardboard.value || opt.cardboard_players.value;
		const auto wants_view_limit = opt.limit_view_distance.value;
		const auto wants_far_clip = opt.limit_far_plane.value;
		const auto wants_scale_cull = opt.cull_small_geometry.value;

		if ( !wants_flat && !wants_cardboard && !wants_view_limit
			&& !wants_far_clip && !wants_scale_cull )
		{
			return;
		}

		const auto flat = ( wants_flat && !wants_cardboard ) ? this->prefixed_material( ) : 0;

		// Два независимых порога. Оба считаются как квадраты заранее: в цикле
		// остаётся одно сравнение, а корень на каждый примитив -- это уже
		// заметная цена на батче в тысячи объектов.
		const auto view_distance_sq = opt.view_distance.value * opt.view_distance.value;
		const auto far_plane_sq = opt.far_plane.value * opt.far_plane.value;
		const auto eye = systems::g_view.origin( );
		const auto min_scale = opt.min_screen_size.value;

		// Сдвиг LOD больше не считается здесь: он применяется на scene
		// object до генерации примитивов, см. on_generate_primitives.

		for ( auto i = 0; i < batch_count; ++i )
		{
			// Примитив scenesystem -- 0x70 байт, материал лежит в +0x20 как
			// указатель на указатель (та же раскладка, что в scene.cpp).
			const auto mesh = batch + ( static_cast< std::size_t >( i ) * 0x70 );
			const auto material = memory::safe_read<std::uintptr_t>( mesh + 0x20 );

			if ( !material || !*material )
			{
				continue;
			}

			const auto source = *material;
			const auto kind = this->classify_material( source );

			if ( this->should_cull( kind ) )
			{
				// Обнуляем материал примитива. Движок пропускает примитив без
				// материала, поэтому запись в саму структуру mesh дешевле и
				// надёжнее, чем попытка убрать его из батча: перестройка
				// батча -- это уже чужой код записи в массив.
				memory::safe_write<std::uintptr_t>( mesh + 0x20, 0 );
				++this->m_culled_objects;
				continue;
			}

			// Порог размера. Поле +0x58 -- единственное, что в примитиве
			// описывает размер, и оно уже используется порогом пропы, поэтому
			// обе настройки работают с одним и тем же числом. Порог берётся
			// как максимум из двух: тогда включение одной галочки не может
			// незаметно усилить другую.
			const auto size_threshold = std::max(
				opt.remove_props.value ? opt.prop_min_scale.value : 0.0f,
				wants_scale_cull ? min_scale : 0.0f );

			if ( size_threshold > 0.0f && ( kind == category::prop || wants_scale_cull ) )
			{
				const auto scale = memory::safe_read<float>( mesh + 0x58 );
				if ( scale && *scale < size_threshold )
				{
					memory::safe_write<std::uintptr_t>( mesh + 0x20, 0 );
					++this->m_culled_objects;
					continue;
				}
			}

			// Дистанция читается один раз на оба порога: origin лежит в
			// примитиве, и второе чтение того же поля -- лишний доступ к
			// памяти в самом горячем месте фичи.
			if ( wants_view_limit || wants_far_clip )
			{
				const auto origin = memory::safe_read<math::vector3>( mesh + 0x40 );
				if ( origin )
				{
					const auto distance_sq = origin->distance_sqr( eye );

					// Дальняя граница режет всё без разбора: она про
					// геометрию мира, у которой нет категории.
					if ( wants_far_clip && distance_sq > far_plane_sq )
					{
						memory::safe_write<std::uintptr_t>( mesh + 0x20, 0 );
						++this->m_culled_objects;
						continue;
					}

					if ( wants_view_limit && distance_sq > view_distance_sq )
					{
						memory::safe_write<std::uintptr_t>( mesh + 0x20, 0 );
						++this->m_culled_objects;
						continue;
					}
				}
			}

			// LOD здесь не трогается намеренно. Поле дистанции в примитиве
			// scenesystem не подтверждено ни одним рабочим местом проекта, а
			// писать наугад в 0x70-байтовую запись батча -- это порча
			// чужой структуры без возможности отката. Принудительный низкий
			// уровень детализации делается там, где бит известен: на scene
			// object, см. on_generate_primitives.

			if ( !wants_cardboard )
			{
				if ( flat )
				{
					memory::safe_write<std::uintptr_t>( mesh + 0x20, flat );
				}
				continue;
			}

			// Картон. Игроки и мир требуют разного шейдера, поэтому признак
			// берётся из имени материала: `models/player` -- единственный
			// надёжный маркер модели персонажа, категорийные списки выше
			// намеренно его не трогают (k_never_touch).
			const auto player_skin = this->is_player_material( source );

			if ( player_skin ? !opt.cardboard_players.value : !opt.cardboard.value )
			{
				if ( flat )
				{
					memory::safe_write<std::uintptr_t>( mesh + 0x20, flat );
				}
				continue;
			}

			const auto cardboard = this->cardboard_material( source, player_skin );
			if ( cardboard )
			{
				memory::safe_write<std::uintptr_t>( mesh + 0x20, cardboard );
			}
		}
	}

	// Принудительный низкий LOD.
	//
	// Вызывается из generate_primitives ДО того, как движок построит
	// примитивы, поэтому смена бита успевает повлиять на текущий кадр.
	// Носитель флага -- scene object, а не примитив: 0x9a это байт
	// LOD-состояния, бит 0x10 в нём означает «брать грубейшую модель».
	// Ровно этот бит уже используется chams'ом (player.chams.cpp), так что
	// раскладка подтверждена работающим кодом, а не догадкой.
	//
	// Бит выставляется только по включённой настройке, но НЕ сбрасывается
	// при выключении: сброс означал бы запись в объекты, которыми владеют
	// другие подсистемы (chams, game_scene_node), и мог бы затереть их
	// собственный LOD-режим. Выключение фичи просто перестаёт форсить бит,
	// а движок вернёт его сам при следующей пересборке объекта.
	void optimization::on_scene_object( std::uintptr_t scene_object ) const
	{
		if ( !this->active( ) || !scene_object )
		{
			return;
		}

		if ( !settings::g_misc.m_optimization.force_low_lod.value )
		{
			return;
		}

		const auto lod = memory::safe_read<std::uint8_t>( scene_object + 0x9a );
		if ( !lod || ( *lod & 0x10 ) )
		{
			return;
		}

		memory::safe_write<std::uint8_t>( scene_object + 0x9a,
			static_cast< std::uint8_t >( *lod | 0x10 ) );
	}

	void optimization::on_light_scene_object( std::uintptr_t object ) const
	{
		if ( !this->active( ) || !object )
		{
			return;
		}

		// Игрок настроил свой свет в scene -- не перебиваем его. Проверяем и
		// мастер-тумблер, и сам факт того, что фича реально включена: иначе
		// выключенный ползунок всё равно бы перекрашивал источники.
		if ( !settings::g_world.m_scene.lighting.value || !settings::g_misc.m_optimization.flat_lighting.value )
		{
			return;
		}

		const auto& opt = settings::g_misc.m_optimization;
		const auto& color = opt.light_color.value;
		const auto intensity = opt.light_intensity.value;

		// Те же три float подряд (r/g/b), что пишет scene.cpp -- раскладка
		// источника света в scenesystem не документирована, поэтому
		// единственная опора тут -- совпадение с уже работающим кодом.
		memory::safe_write<float>( object + 0xe4, static_cast<float>( color.r ) / 255.0f * intensity );
		memory::safe_write<float>( object + 0xe8, static_cast<float>( color.g ) / 255.0f * intensity );
		memory::safe_write<float>( object + 0xec, static_cast<float>( color.b ) / 255.0f * intensity );
	}

	__m128i* optimization::intercept_shader_param( std::uint32_t hash, __m128i* value )
	{
		if ( !this->active( ) )
		{
			return value;
		}

		const auto& opt = settings::g_misc.m_optimization;

		// Хеши пост-обработки Source 2 (Murmur2, case-insensitive). Те же
		// значения, которыми уже управляет scene.cpp -- здесь мы их только
		// гасим, поэтому список обязан совпадать.
		enum : std::uint32_t
		{
			bloom_scale = 0x565EAF76,
			bloom_threshold = 0xBA98A9B0,
			bloom_width = 0x2AE72B37,
			bloom_strength = 0xB692902E,
			bloom_skybox = 0x1313A424,
			dof_ranges = 0x2ACAB07C,
			motion_blur = 0x6B2E7C41,
			ssao_strength = 0x1F42D9A3,
			ssao_radius = 0x4C81B7E0,
		};

		s_zero = _mm_setzero_ps( );

		if ( opt.remove_bloom.value )
		{
			switch ( hash )
			{
			case bloom_scale:
			case bloom_strength:
			case bloom_skybox:
				// Множители яркости -- ноль выключает сам проход.
				return reinterpret_cast< __m128i* >( &s_zero );

			case bloom_threshold:
				// Порог -- в потолок: даже если проход останется, ни один
				// пиксель его не перешагнёт.
				s_zero = _mm_set_ps1( 1.0e6f );
				return reinterpret_cast< __m128i* >( &s_zero );

			case bloom_width:
				s_zero = _mm_set_ps1( 0.0f );
				return reinterpret_cast< __m128i* >( &s_zero );

			default:
				break;
			}
		}

		if ( opt.remove_dof.value && hash == dof_ranges )
		{
			// near_blurry = far_blurry = 0 и огромный crisp-диапазон: зона
			// резкости накрывает всю сцену, размывать нечего.
			s_color = _mm_set_ps( 0.0f, 1.0e6f, 1.0e6f, 0.0f );
			return reinterpret_cast< __m128i* >( &s_color );
		}

		if ( opt.remove_motion_blur.value && hash == motion_blur )
		{
			return reinterpret_cast< __m128i* >( &s_zero );
		}

		if ( opt.remove_ssao.value && ( hash == ssao_strength || hash == ssao_radius ) )
		{
			return reinterpret_cast< __m128i* >( &s_zero );
		}

		return value;
	}

	bool optimization::override_fog( __m128i* output, int* mode ) const
	{
		if ( !this->active( ) || !output || !mode )
		{
			return false;
		}

		// Туман -- это полноэкранный проход по градиенту. Если игрок не
		// просил свой туман (см. weather.cpp), глушим ванильный: рисуем
		// бесконечную дистанцию и нулевую плотность.
		const auto& weather = settings::g_world.m_weather;
		if ( weather.fog_enabled.value )
		{
			return false;
		}

		const auto set_param_f = PATTERN( patterns::set_shader_param );
		const auto set_param_i = PATTERN( patterns::set_shader_param_i );
		if ( !set_param_f || !set_param_i )
		{
			return false;
		}

		constexpr std::uint32_t gradient_fog{ 0x4B01FF63 };
		constexpr std::uint32_t gradient_fog_2{ 0x0AA49C2A };
		constexpr std::uint32_t gradient_fog_3{ 0xFBF6448D };
		constexpr std::uint32_t enable_gradient_fog{ 0x6E0FAD7E };

		s_fog_params = _mm_set_ps( 0.0f, 0.0f, 1.0e6f, 0.0f );
		s_fog_params_2 = _mm_set_ps( 0.0f, 0.0f, 0.0f, 0.0f );
		s_fog_params_3 = _mm_set_ps( 0.0f, 0.0f, 0.0f, 0.0f );

		memory::call<std::uintptr_t>( set_param_f, output, gradient_fog, &s_fog_params );
		memory::call<std::uintptr_t>( set_param_f, output, gradient_fog_2, &s_fog_params_2 );
		memory::call<std::uintptr_t>( set_param_f, output, gradient_fog_3, &s_fog_params_3 );
		memory::call<std::uintptr_t>( set_param_i, output + 17, enable_gradient_fog, 1 );

		*mode = 0;
		return true;
	}

	void optimization::update_stats( float frame_time )
	{
		constexpr auto smoothing = 0.05f;

		this->m_smoothed_frame_time = std::lerp(
			this->m_smoothed_frame_time, frame_time, smoothing );

		const auto ratio = this->m_seen_objects
			? static_cast< float >( this->m_culled_objects )
				/ static_cast< float >( this->m_seen_objects )
			: 0.0f;

		this->m_smoothed_cull_ratio = std::lerp(
			this->m_smoothed_cull_ratio, ratio, smoothing );
	}

	void optimization::on_render( xdraw::draw_list& draw_list )
	{
		if ( !this->active( ) || !settings::g_misc.m_optimization.show_stats.value )
		{
			return;
		}

		this->update_stats( xdraw::delta_time( ) );

		const auto fps = xdraw::framerate( );
		const auto ms = this->m_smoothed_frame_time * 1000.0f;
		const auto cull = this->m_smoothed_cull_ratio * 100.0f;

		const auto text = std::format(
			"opt {:.0f} fps  {:.2f} ms  culled {:.0f}%", fps, ms, cull );

		// Панель прижата к правому верхнему углу: не мешает ни киллфиду, ни
		// хитмаркерам, ни меню.
		const auto [text_w, text_h] = xdraw::measure_text( text );
		const auto [screen_w, screen_h] = xdraw::viewport_size( );

		const auto x = static_cast< float >( screen_w ) - text_w - 12.0f;
		const auto y = 72.0f;

		// Фон под текстом: без него на светлых картах (Mirage, Dust 2)
		// цифры не читаются.
		draw_list.rect_filled(
			x - 6.0f, y - 3.0f, text_w + 12.0f, text_h + 6.0f, { 0, 0, 0, 140 } );

		draw_list.text( x, y, text, { 220, 235, 255, 255 } );
	}

} // namespace features::misc
