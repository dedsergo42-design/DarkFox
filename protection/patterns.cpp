#include <pch/pch.hpp>
#include <protection/game_addresses.hpp>

namespace patterns {

	const ::protection::addresses::address_t& add_entity = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:33F6488D9F????????4889B7????????488D05*????????48890733D2+78~"),
		::protection::addresses::address_type::pattern,
		"client.dll:33F6488D9F????????4889B7????????488D05*????????48890733D2+78~");

	const ::protection::addresses::address_t& base_fire_guns_get_inaccuracy = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????84C00F84C6FEFFFF"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????84C00F84C6FEFFFF");

	const ::protection::addresses::address_t& button_state_alloc = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B54CA088D4101894708EB16488B0F>E8????????488BD0488BCF-80"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B54CA088D4101894708EB16488B0F>E8????????488BD0488BCF-80");

	const ::protection::addresses::address_t& cmd_interpreter = ADDRESS_IMPL(
		::protection::addresses::hash("rendersystemdx11.dll:>E8????????4183BDC000000000"),
		::protection::addresses::address_type::pattern,
		"rendersystemdx11.dll:>E8????????4183BDC000000000");

	const ::protection::addresses::address_t& create_move = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:FFFFFFFF488D05*????????48890D????????+28~"),
		::protection::addresses::address_type::pattern,
		"client.dll:FFFFFFFF488D05*????????48890D????????+28~");

	const ::protection::addresses::address_t& csgo_input = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:84C0740C488D0D*????????E8????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:84C0740C488D0D*????????E8????????");

	const ::protection::addresses::address_t& draw_flash_effect = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:85D20F88????????48894C24??5556"),
		::protection::addresses::address_type::pattern,
		"client.dll:85D20F88????????48894C24??5556");

	// Обновлено после апдейта 2026-09-26.
	// Прежний якорь `movss xmm0,[rdx+?]` сразу за `sub rsp` компилятор убрал:
	// теперь после кадра идут четыре сохранения аргументов
	// (`mov rbx,r9; mov rsi,r8; mov rdx,r10; mov r15,rcx` -- это ровно пять
	// параметров хука), затем ранний null-check `test rdx,rdx`.
	// Кадр 0x500 отличает эту функцию от соседней с кадром 0x200 (0xD0DC00).
	const ::protection::addresses::address_t& draw_legs = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4055535641564157488DAC2400FCFFFF4881EC00050000498BD9498BF04C"),
		::protection::addresses::address_type::pattern,
		"client.dll:4055535641564157488DAC2400FCFFFF4881EC00050000498BD9498BF04C");

	const ::protection::addresses::address_t& draw_overhead = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:40534883EC??488BD983FA??75??"),
		::protection::addresses::address_type::pattern,
		"client.dll:40534883EC??488BD983FA??75??");

	const ::protection::addresses::address_t& draw_scene_object = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:488D05*????????488907488B7C2448+8~"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:488D05*????????488907488B7C2448+8~");

	const ::protection::addresses::address_t& draw_scene_object_array = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:488BC4488950??488948??555356574154415541564157488DA8????????4881EC????????0F2970??"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:488BC4488950??488948??555356574154415541564157488DA8????????4881EC????????0F2970??");

	const ::protection::addresses::address_t& draw_skybox_array = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:4585C90F8E????????4C8BDC"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:4585C90F8E????????4C8BDC");

	const ::protection::addresses::address_t& dynamic_light_alloc = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488BD94533C0488B0D????????BA01000000>E8????????8B0B"),
		::protection::addresses::address_type::pattern,
		"client.dll:488BD94533C0488B0D????????BA01000000>E8????????8B0B");

	// Обновлено после апдейта 2026-09-26.
	// Глобал-слот менеджера динамического света переехал. Хвост
	// `E8????????E8????????BAFFFFFFFF` исчез, но голова
	// (`mov rcx,[rip+?]; test rcx,rcx; jz; mov rdx,rdi`) уникальна
	// и резолвит тот же слот (0x25580F8) -- `*` указывает на операнд.
	const ::protection::addresses::address_t& dynamic_light_manager = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????4885C97408488BD7"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????4885C97408488BD7");

	const ::protection::addresses::address_t& dynamic_light_time = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:40534883EC20488BD985D2741A488B05????????F30F104830"),
		::protection::addresses::address_type::pattern,
		"client.dll:40534883EC20488BD985D2741A488B05????????F30F104830");

	// Обновлено после апдейта 2026-09-26 (engine2.dll).
	// Прежний хвост `BA FFFFFFFF; lea rcx,[rip+?]; call; test rax,rax; jnz`
	// развалился: компилятор выбросил сравнение с -1 и переставил загрузку
	// глобалов сразу за гардом `cmp eax,1; jbe +0x25`. Пролог (frame 0x170,
	// четыре callee-saved push + xmm6 в теневом слоте) уникален -- одно
	// совпадение (RVA 0x758D0).
	const ::protection::addresses::address_t& engine_client_cmd = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:488BC448895808488968104889701857415641574881EC700100000F2970D8410FB6E98D42FC4D8BF88BFA4C8BF183F8017625"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:488BC448895808488968104889701857415641574881EC700100000F2970D8410FB6E98D42FC4D8BF88BFA4C8BF183F8017625");

	const ::protection::addresses::address_t& entity_list = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????8BFBC1EB0E"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????8BFBC1EB0E");

	// Обновлено после апдейта 2026-09-26 (filesystem_stdio.dll).
	// Прежний якорь `>E8????????FFD34C8BA42498000000` терял цель: хвост
	// `call rax; mov r12,[rsp+0x98]` переставлен. Теперь вызывающий код
	// начинается с `lea rcx,[rdi-0xE0]; mov edx,ebp`, а за самим вызовом
	// стоит `call rbx; lea r11,[rsp+0x60]` -- этот хвост уникален
	// (одно совпадение, цель вызова RVA 0x4C5E0 → filesystem_close).
	const ::protection::addresses::address_t& filesystem_close = ADDRESS_IMPL(
		::protection::addresses::hash("filesystem_stdio.dll:488D8F20FFFFFF8BD5>E8????????FFD34C8D5C2460"),
		::protection::addresses::address_type::pattern,
		"filesystem_stdio.dll:488D8F20FFFFFF8BD5>E8????????FFD34C8D5C2460");

	// Обновлено после апдейта 2026-09-26.
	// Хвост `test rax,rax; lea rsi,[rbp+0x1C]` переставлен: теперь сравнение
	// идёт ПОСЛЕ вызова FindElement(this,name), а перед ним стоит уникальный
	// гард `shr eax,2; test al,1; je +0x2A` (индекс слота хада).
	const ::protection::addresses::address_t& find_hud_element = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D55D8488BCB>E8????????8B4710C1E802A801742A488B4728488D4DE84885C0488D75D8"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D55D8488BCB>E8????????8B4710C1E802A801742A488B4728488D4DE84885C0488D75D8");

	const ::protection::addresses::address_t& frame_input_ring_base = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:488D05*????????0F1004C8"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:488D05*????????0F1004C8");

	const ::protection::addresses::address_t& frame_input_ring_idx = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:486315*????????83FA0A7D61"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:486315*????????83FA0A7D61");

	const ::protection::addresses::address_t& frame_stage_notify = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C241848896C2420574883EC40488BF9"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C241848896C2420574883EC40488BF9");

	const ::protection::addresses::address_t& game_entity_system = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????EB028BC6"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????EB028BC6");

	const ::protection::addresses::address_t& game_event_get_controller = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D05*????????4D8BF8488901+80~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D05*????????4D8BF8488901+80~");

	const ::protection::addresses::address_t& game_event_get_float = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????0F28D8895C2420"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????0F28D8895C2420");

	const ::protection::addresses::address_t& game_event_get_int = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????3D00800000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????3D00800000");

	const ::protection::addresses::address_t& game_event_get_pawn = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D05*????????4D8BF8488901+88~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D05*????????4D8BF8488901+88~");

	const ::protection::addresses::address_t& game_event_get_string = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????85DB0F9FC3"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????85DB0F9FC3");

	const ::protection::addresses::address_t& game_event_manager = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????488B01FF50??FFC3~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????488B01FF50??FFC3~");

	const ::protection::addresses::address_t& game_rules = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????4C897010"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????4C897010");

	const ::protection::addresses::address_t& game_scene_node_set_mesh_group = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????8B852C850100"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????8B852C850100");

	const ::protection::addresses::address_t& game_scene_node_set_skeleton = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4084ED7417"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4084ED7417");

	const ::protection::addresses::address_t& game_trace_manager = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????488D3452~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????488D3452~");

	const ::protection::addresses::address_t& generate_primitives = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:488D05*????????488907488B7C2448+20~"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:488D05*????????488907488B7C2448+20~");

	// get_aim_punch: паттерн УДАЛЁН, а не починен.
	//
	// Он искал место вызова игрового геттера отдачи. После обновления игры эта
	// форма исчезла, и сигнатура перестала находиться -- но хуже было не это:
	// неразрешённый адрес молча давал нулевой punch, выстрел уходил по
	// некомпенсированному углу, и ragebot мазал тем сильнее, чем длиннее была
	// очередь.
	//
	// Компенсация отдачи больше не зависит от сигнатуры: get_aim_punch в
	// shared.cpp читает обе составляющие прямо из схемы
	// (CCSPlayer_AimPunchServices::m_predictableBaseAngle / m_unpredictableBaseAngle).
	// Смещения приходят из дампа, поэтому обновление игры ломать компенсацию
	// не может в принципе. Паттерн не нужен и только засорял список ненайденных.

	const ::protection::addresses::address_t& get_bone_index = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:448B42??488B12E9"),
		::protection::addresses::address_type::pattern,
		"client.dll:448B42??488B12E9");

	const ::protection::addresses::address_t& get_glow_color = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????F30F10BE????????488BCF"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????F30F10BE????????488BCF");

	const ::protection::addresses::address_t& get_inaccuracy = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??5556574881EC????????440F298424"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??5556574881EC????????440F298424");

	const ::protection::addresses::address_t& get_interp_amount = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????418B9668030000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????418B9668030000");

	const ::protection::addresses::address_t& get_interpolated_shoot_position = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:40555641564881EC20010000"),
		::protection::addresses::address_type::pattern,
		"client.dll:40555641564881EC20010000");

	// C_CSWeaponBase::GetSpread(): vdata = [rcx+0x388]; mode = [rcx+0x1A00];
	// if (mode >= 0 && mode < 2) return vdata->m_flSpread[mode]; else return vdata->m_flSpread[0];
	// Отличие от близнеца GetMaxSpeed (0x748) — точный disp 0x750 (m_flSpread), он и делает паттерн уникальным.
	const ::protection::addresses::address_t& get_spread = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:486391001A0000488B818803000085D278??4883FA0273??F30F10849050070000"),
		::protection::addresses::address_type::pattern,
		"client.dll:486391001A0000488B818803000085D278??4883FA0273??F30F10849050070000");

	const ::protection::addresses::address_t& get_net_channel = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:4C8B05????????4D85C07410"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:4C8B05????????4D85C07410");

	const ::protection::addresses::address_t& get_tick_view_angles = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C2408574881ECF0000000F30F100A488D8C2410010000418BD8488BFAE8????????F30F104F04"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C2408574881ECF0000000F30F100A488D8C2410010000418BD8488BFAE8????????F30F104F04");

	const ::protection::addresses::address_t& get_transforms_for_hitbox_list = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??555657415441554881EC????????4963304D8BE0488BEA488BD985F6"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??555657415441554881EC????????4963304D8BE0488BEA488BD985F6");

	const ::protection::addresses::address_t& get_usercmd = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:40534883EC208BDAE8????????4C8BC0"),
		::protection::addresses::address_type::pattern,
		"client.dll:40534883EC208BDAE8????????4C8BC0");

	const ::protection::addresses::address_t& get_usercmd_base = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4883EC28>E8????????8B8010590000"),
		::protection::addresses::address_type::pattern,
		"client.dll:4883EC28>E8????????8B8010590000");

	const ::protection::addresses::address_t& get_view_angles = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:8B0D????????8BD3>E8????????F20F1000"),
		::protection::addresses::address_type::pattern,
		"client.dll:8B0D????????8BD3>E8????????F20F1000");

	const ::protection::addresses::address_t& get_world_group_handle = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????418B5F10"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????418B5F10");

	// Обновлено после апдейта 2026-09-26.
	// Прежний якорь резолвил ЦЕЛЬ вызова по хвосту `movss xmm6,[rsi-0x8A]`,
	// но компилятор пересобрал вызывающую функцию -- байтов `F3 41 0F 10 B6`
	// с этой диспозицией в образе больше нет. Якоримся на САМУ функцию
	// (RVA 0x9EA070): тело `mov rax,[rcx+0x30]; test rax,rax; je; mov
	// rax,[rax+0x10]; mov ecx,[rax+0x38]; mov rax,rdx; mov [rdx],ecx; ret` --
	// это ровно `int* get_world_group_id(CGameSceneNode* rcx, int* rdx)`:
	// владелец сцены (+0x30) -> entity identity (+0x10) -> m_worldGroupId
	// (+0x38) пишется в out и возвращается. Проверено по сайту вызова
	// RVA 0xAC5AE7: `call 0x9EA070` затем `mov edx,[rax]` и
	// `call 0x3B9570` (get_world_group_handle) -- та же пара, что в
	// player.chams.cpp. Одно совпадение.
	const ::protection::addresses::address_t& get_world_group_id = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B41304885C0740D488B40108B4838488BC2890AC3"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B41304885C0740D488B40108B4838488BC2890AC3");

	const ::protection::addresses::address_t& global_vars = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B05*????????448B4044"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B05*????????448B4044");

	const ::protection::addresses::address_t& handle_view_angles = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:FFFFFFFF488D05*????????48890D????????+40~"),
		::protection::addresses::address_type::pattern,
		"client.dll:FFFFFFFF488D05*????????48890D????????+40~");

	const ::protection::addresses::address_t& history_field_alloc = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????488BD0488D4E28"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????488BD0488D4E28");

	// CPlayer_MovementServices::QuantizeMovement( services, c_usercmd )
	//
	// Rounds the command's three analog axes in place when
	// sv_quantize_movement_input is on:
	//
	//   usercmd+0x2C = V_roundf( usercmd+0x2C )   forward
	//   usercmd+0x30 = V_roundf( usercmd+0x30 )   left
	//   usercmd+0x34 = V_roundf( usercmd+0x34 )   up
	//
	// Nothing else is touched -- in particular the subtick analog deltas are left
	// alone, which is the detail an earlier attempt at this got wrong.
	const ::protection::addresses::address_t& quantize_movement = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24?574883EC?488BDA488BF9E8????????33C9"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24?574883EC?488BDA488BF9E8????????33C9");

	const ::protection::addresses::address_t& hud = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B05*????????4885C07471"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B05*????????4885C07471");

	// Roots of the two Panorama trees.
	//
	//   main_menu_panel   EC ? 48 8B 05 ? ? ? ? 48 8D 15 ? ? ? ? 48   +5, len 9
	//   csgo_hud_panel    48 89 35 ? ? ? ? E8 ? ? ? ? 48 85           +3, len 7
	//
	// Both are rip-relative loads of a global, which is what '*' means here: the
	// operand sits at the marked byte and the result is operand + 4 + rel. That
	// is the same arithmetic as the ResolveRelativeAddress( addr, offset, length )
	// these came as, since offset + 4 == length in both.
	const ::protection::addresses::address_t& csgo_hud_panel = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488935*????????E8????????4885"),
		::protection::addresses::address_type::pattern,
		"client.dll:488935*????????E8????????4885");

	// Обновлено после апдейта 2026-09-26.
	// Прежний вид `EC?488B05*????????488D15????????48` совпадал с ДВУМЯ
	// функциями-обходчиками панельного дерева (0xBFC440 и 0xDD0E40): обе
	// грузят глобал и строку через rip и дёргают виртуальные методы панели.
	// Развёл по строке: у 0xBFC440 `lea rdx,[rip+?]` указывает на
	// "lockedState" -- состояние ГЛАВНОГО меню (у HUD-панели его нет),
	// у 0xDD0E40 -- на таблицу VA. Якорь -- начало функции (RVA 0xBFC440)
	// и её первые инструкции до и включая вызов слота 0x178.
	// Одно совпадение.
	const ::protection::addresses::address_t& main_menu_panel = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4883EC28488B05????????488D15????????488B4808488B01FF9078010000"),
		::protection::addresses::address_type::pattern,
		"client.dll:4883EC28488B05????????488D15????????488B4808488B01FF9078010000");

	const ::protection::addresses::address_t& hud_death_notice_clear = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:85C07509488D4EE0>E8????????488B5C2440"),
		::protection::addresses::address_type::pattern,
		"client.dll:85C07509488D4EE0>E8????????488B5C2440");

	const ::protection::addresses::address_t& hud_weapon_selection_update = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:498BE35FC3488BCB>E8????????488BCBE8????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:498BE35FC3488BCB>E8????????488BCBE8????????");

	const ::protection::addresses::address_t& init_particle_path_buffer = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??574883EC??8B41??488D79"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??574883EC??8B41??488D79");

	const ::protection::addresses::address_t& init_particle_path_buffer_alt = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??574883EC??8B41??488D79"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??574883EC??8B41??488D79");

	const ::protection::addresses::address_t& is_glowing = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:0000488BEA488BF9>E8????????4533F684C0"),
		::protection::addresses::address_type::pattern,
		"client.dll:0000488BEA488BF9>E8????????4533F684C0");

	const ::protection::addresses::address_t& item_system = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4883EC28488B05????????4885C00F8581"),
		::protection::addresses::address_type::pattern,
		"client.dll:4883EC28488B05????????4885C00F8581");

	const ::protection::addresses::address_t& kv3_alloc = ADDRESS_IMPL(
		::protection::addresses::hash("tier0.dll:40534883EC3080FA060FB6C241B916"),
		::protection::addresses::address_type::pattern,
		"tier0.dll:40534883EC3080FA060FB6C241B916");

	const ::protection::addresses::address_t& kv3_destroy = ADDRESS_IMPL(
		::protection::addresses::hash("tier0.dll:405741574883EC384C8B01448BFA498BC0488BF948C1E802"),
		::protection::addresses::address_type::pattern,
		"tier0.dll:405741574883EC384C8B01448BFA498BC0488BF948C1E802");

	const ::protection::addresses::address_t& kv3_load = ADDRESS_IMPL(
		::protection::addresses::hash("tier0.dll:44242848897C2420>E8????????0FB6D88B4C2444"),
		::protection::addresses::address_type::pattern,
		"tier0.dll:44242848897C2420>E8????????0FB6D88B4C2444");

	const ::protection::addresses::address_t& level_initialization = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D05*????????C6411000+B8~"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D05*????????C6411000+B8~");

	const ::protection::addresses::address_t& level_shutdown = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4883EC??488B0D????????488D15????????4533C94533C0488B01FF50304885C074??488B0D????????488BD04C8B0141FF50404883C4??"),
		::protection::addresses::address_type::pattern,
		"client.dll:4883EC??488B0D????????488D15????????4533C94533C0488B01FF50304885C074??488B0D????????488BD04C8B0141FF50404883C4??");

	const ::protection::addresses::address_t& light_data_queue = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:488B05*????????48C1E104+8"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:488B05*????????48C1E104+8");

	const ::protection::addresses::address_t& light_scene_object = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:>E8????????440F285C2460"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:>E8????????440F285C2460");

	const ::protection::addresses::address_t& local_player_controller = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48391D*????????7504B001"),
		::protection::addresses::address_type::pattern,
		"client.dll:48391D*????????7504B001");

	const ::protection::addresses::address_t& log_internal = ADDRESS_IMPL(
		::protection::addresses::hash("tier0.dll:>E8????????448B55B3"),
		::protection::addresses::address_type::pattern,
		"tier0.dll:>E8????????448B55B3");

	const ::protection::addresses::address_t& material_create = ADDRESS_IMPL(
		::protection::addresses::hash("materialsystem2.dll:48895C24??48896C24??48897424??48897C24??41564881EC????????488B05????????488BF2"),
		::protection::addresses::address_type::pattern,
		"materialsystem2.dll:48895C24??48896C24??48897424??48897C24??41564881EC????????488B05????????488BF2");

	const ::protection::addresses::address_t& material_manager = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????80A5E7000000EF"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????80A5E7000000EF");

	const ::protection::addresses::address_t& override_view = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:A8000000488D05*????????4C89742420+78~"),
		::protection::addresses::address_type::pattern,
		"client.dll:A8000000488D05*????????4C89742420+78~");

	// Обновлено после апдейта 2026-09-26.
	// Старый вид оканчивался на `lea rax,[rip+0x14AE08A]` -- абсолютную
	// диспозицию vtable, она переехала вместе с .rdata. Голова пролога
	// (`mov [rsp+18],rbx; mov [rsp+20],rsi; push rdi; sub rsp,20; lea rax,[rip+?]`)
	// разделяется 298 функциями-сиблингами (все deleting-dtor'ы CCSUsrMsg_*),
	// поэтому одного `lea` мало -- якоримся на хвост ПОСЛЕ него.
	// Целевой класс опознан по RTTI: vtable 0x1B49F30 -> COL 0x1ED4140 ->
	// type descriptor ".?AVCCSUsrMsg_ReportHit@@" (0x22D8B10). Сама функция
	// deleting-деструктора -- RVA 0x58E860.
	// Уникальность даёт хвост после disp32: `mov esi,edx; mov [rcx],rax;
	// mov rbx,rcx; test byte ptr [r8+8],1` -- проверка "deleting" и
	// вызов `~CCSUsrMsg_ReportHit` по адресу в vtable.
	const ::protection::addresses::address_t& parse_report_hit = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24184889742420574883EC20488D05????????8BF2488901488BD9F6410801740F4883C108E87385"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24184889742420574883EC20488D05????????8BF2488901488BD9F6410801740F4883C108E87385");

	const ::protection::addresses::address_t& particle_create_effect = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4C8BDC534881EC90000000F20F1005"),
		::protection::addresses::address_type::pattern,
		"client.dll:4C8BDC534881EC90000000F20F1005");

	const ::protection::addresses::address_t& particle_destroy_effect = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:83FAFF0F84????????4154"),
		::protection::addresses::address_type::pattern,
		"client.dll:83FAFF0F84????????4154");

	const ::protection::addresses::address_t& particle_manager = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B35*????????44896C24??"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B35*????????44896C24??");

	const ::protection::addresses::address_t& particle_set_control_point = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4883EC58F3410F105104F3410F1009F3410F105908"),
		::protection::addresses::address_type::pattern,
		"client.dll:4883EC58F3410F105104F3410F1009F3410F105908");

	const ::protection::addresses::address_t& particle_set_entity_binding = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4154415541574881EC900000004D8BF9"),
		::protection::addresses::address_type::pattern,
		"client.dll:4154415541574881EC900000004D8BF9");

	const ::protection::addresses::address_t& particle_set_transform = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??48896C24??48897424??574883EC40488BF9498BE9"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??48896C24??48897424??574883EC40488BF9498BE9");

	const ::protection::addresses::address_t& planted_c4 = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B1D*????????488BD34C8B81"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B1D*????????488BD34C8B81");

	// Обновлено после апдейта 2026-09-26.
	// Функция переехала (0x... -> RVA 0xBE8EE0), кадр уменьшился: `lea rbp,
	// [rsp-0x350]` -> `[rsp-0x300]`, `sub rsp,0x450` -> `0x400`. Хвост
	// `mov [rbp-0x7C],eax` заменён на `mov [rsp+0x58],eax` (esi сохраняет
	// то же возвращаемое значение, r12 -- тот же this). Опознаётся по
	// неизменной сигнатуре: пролог (rbx/rsi в теневых слотах + семь push)
	// и виртуальный диспетчер `mov rcx,[rip+?]; mov rax,[rcx];
	// call [rax+0xB8]; mov rcx,rax; mov rdx,[rax]; call [rdx+0x38]`.
	// Диспозиция `lea rbp` завайлдкардена -- одно совпадение.
	const ::protection::addresses::address_t& post_network_data_received = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C241048894C24085556574154415541564157488DAC24????????4881EC000400004C8BF1488B0D????????488B01FF90B8000000488BC8488B10FF5238"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C241048894C24085556574154415541564157488DAC24????????4881EC000400004C8BF1488B0D????????488B01FF90B8000000488BC8488B10FF5238");

	// Обновлено после апдейта 2026-09-26 (дамп 14185, client.dll 01:22).
	// Старый вид `>E8????????488B8398010000` резолвил ЦЕЛЬ вызова по хвосту
	// `mov rax,[rbx+0x198]`. Компилятор переставил код: хвост остался, но call
	// уехал ЗА него, и паттерн перестал совпадать. Ищем саму функцию по телу:
	// CPlayer_MovementServices::FinishMove обнуляет speed/velocity в move_data
	// (r8) и ставит последний член = 1.0f -- эти четыре записи уникальны.
	const ::protection::addresses::address_t& prediction_finish_move = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4C8968D84C8970D04533F64C8978C84589706045897078"),
		::protection::addresses::address_type::pattern,
		"client.dll:4C8968D84C8970D04533F64C8978C84589706045897078");

	// Обновлено после апдейта 2026-09-26.
	// Глобал dwPrediction переехал: 0x255C4B0 -> 0x25605E0 (RVA). Пятый байт
	// хвостового jmp сменился (E9BC8791 -> E9BCB791); если оставить только
	// 4883EC28/488D0D/E8, шаблон ловит четыре разных функции.
	const ::protection::addresses::address_t& prediction_player = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4883EC28488D0D*????????E8????????488D0D????????4883C428E9BCB791"),
		::protection::addresses::address_type::pattern,
		"client.dll:4883EC28488D0D*????????E8????????488D0D????????4883C428E9BCB791");

	// Обновлено после апдейта 2026-09-26.
	// Старый вид резолвил цель вызова по префиксу `mov r10,rax; mov rcx,r13;
	// mov eax,[r8+0x44]`, но префикс изменился. Теперь якорь -- пролог самой
	// CPlayer_MovementServices::DoMovement плюс следующая за ним lea логгера,
	// что даёт ровно одно совпадение.
	const ::protection::addresses::address_t& prediction_process_movement = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4C89B42438010000488D052D211D01"),
		::protection::addresses::address_type::pattern,
		"client.dll:4C89B42438010000488D052D211D01");

	const ::protection::addresses::address_t& prediction_reset_pawn = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B114885D274??806A????75??488B05????????8B4044894218"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B114885D274??806A????75??488B05????????8B4044894218");

	const ::protection::addresses::address_t& prediction_seed = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:8B3D*????????488B03488BCB"),
		::protection::addresses::address_type::pattern,
		"client.dll:8B3D*????????488B03488BCB");

	const ::protection::addresses::address_t& prediction_set_pawn = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??574883EC2048C70100000000488BFA488BD94885D274??488B02"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??574883EC2048C70100000000488BFA488BD94885D274??488B02");

	const ::protection::addresses::address_t& prediction_set_state = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:8B81????????84D274??FFC08981????????C383E8018981????????75??80B9"),
		::protection::addresses::address_type::pattern,
		"client.dll:8B81????????84D274??FFC08981????????C383E8018981????????75??80B9");

	// Обновлено после апдейта 2026-09-26.
	// CPlayer_MovementServices::SetupMove: пролог сохраняет rbx в теневом
	// слоте и валит шесть callee-saved регистров подряд -- вместе с lea
	// логгера это уникальный якорь (три совпадения без lea, одно с ним).
	const ::protection::addresses::address_t& prediction_setup_move = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C241855565741544155415641574883EC70488D05"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C241855565741544155415641574883EC70488D05");

	const ::protection::addresses::address_t& prediction_state = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????33D28B5B38"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????33D28B5B38");

	const ::protection::addresses::address_t& prepare_scene_material = ADDRESS_IMPL(
		::protection::addresses::hash("materialsystem2.dll:48895C24084889742410574883EC30488B5920"),
		::protection::addresses::address_type::pattern,
		"materialsystem2.dll:48895C24084889742410574883EC30488B5920");

	const ::protection::addresses::address_t& process_input_event = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:CCCC48895C2408574883EC20C6410800488D05*????????488901488BD9+20~"),
		::protection::addresses::address_type::pattern,
		"client.dll:CCCC48895C2408574883EC20C6410800488D05*????????488901488BD9+20~");

	const ::protection::addresses::address_t& read_frame_input = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4D8BC58BD3"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4D8BC58BD3");

	const ::protection::addresses::address_t& remove_entity = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:33F6488D9F????????4889B7????????488D05*????????48890733D2+80~"),
		::protection::addresses::address_type::pattern,
		"client.dll:33F6488D9F????????4889B7????????488D05*????????48890733D2+80~");

	// Обновлено после апдейта 2026-09-26.
	// Прежний якорь `mov rcx,rax; mov [rsp+0x30],dil` компилятор переставил.
	// Теперь хвост `test al,al; je +0x9B` стоит после пары вызовов:
	// `movdqa [rbp+0x10],xmm0; call; mov rcx,rdi; call <crosshair>`.
	// Второй `>` резолвит саму функцию отрисовки прицела.
	const ::protection::addresses::address_t& render_crosshair = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:660F7F4510>E8????????488BCF>E8????????84C00F849B000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:660F7F4510>E8????????488BCF>E8????????84C00F849B000000");

	// Обновлено после апдейта 2026-09-26.
	// Хвост `BA FF FF FF FF` (mov edx,-1) за вызовом исчез из сборки, поэтому
	// паттерн перестал совпадать. Префикс вызова уникален сам по себе:
	// `mov al,1; mov rdx,rdi; mov rcx,r13; call render_decals` -- один сайт
	// во всём client.dll. Якорь -- последний байт следующей инструкции (48).
	const ::protection::addresses::address_t& render_decals = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:B001488BD7498BCD>E8????????48"),
		::protection::addresses::address_type::pattern,
		"client.dll:B001488BD7498BCD>E8????????48");

	const ::protection::addresses::address_t& render_game_system_storage = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B0D*????????418BD6E8????????418B5F"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B0D*????????418BD6E8????????418B5F");

	const ::protection::addresses::address_t& render_scope = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488BC453574883EC68488BFA"),
		::protection::addresses::address_type::pattern,
		"client.dll:488BC453574883EC68488BFA");

	const ::protection::addresses::address_t& render_smoke = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:5C24284889442420>E8????????488B5C2460"),
		::protection::addresses::address_type::pattern,
		"client.dll:5C24284889442420>E8????????488B5C2460");

	// Обновлено после апдейта 2026-09-26.
	// Хвост `lea rax,[rip+..]; mov qword [rsp+0x48],?` компилятор переставил:
	// в новой сборке сразу за sub rsp идёт movzx из .data-ворда (0x25B9370),
	// а самого lea в прологе больше нет. Якорь -- пролог + размер кадра 0xE8:
	// он один такой, стек 0xA8 принадлежит другой функции (0x164B270).
	const ::protection::addresses::address_t& render_view = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4C8BDC535556574881ECE80000000FB705"),
		::protection::addresses::address_type::pattern,
		"client.dll:4C8BDC535556574881ECE80000000FB705");

	const ::protection::addresses::address_t& resource_system_load = ADDRESS_IMPL(
		::protection::addresses::hash("resourcesystem.dll:48895C24??48896C24??48897424??574883EC??488B01"),
		::protection::addresses::address_type::pattern,
		"resourcesystem.dll:48895C24??48896C24??48897424??574883EC??488B01");

	const ::protection::addresses::address_t& get_resource_view = ADDRESS_IMPL(
		::protection::addresses::hash("rendersystemdx11.dll:48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 48 89 4C 24 ?? 55 41 54 41 55 41 56 41 57 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 33 FF 4D 0F BE F8 89 7D ?? 45 0F B6 E1 4C 8B 2D"),
		::protection::addresses::address_type::pattern,
		"rendersystemdx11.dll:48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 48 89 4C 24 ?? 55 41 54 41 55 41 56 41 57 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 33 FF 4D 0F BE F8 89 7D ?? 45 0F B6 E1 4C 8B 2D");

	const ::protection::addresses::address_t& resource_system_precache = ADDRESS_IMPL(
		::protection::addresses::hash("resourcesystem.dll:405355574881EC80000000"),
		::protection::addresses::address_type::pattern,
		"resourcesystem.dll:405355574881EC80000000");

	const ::protection::addresses::address_t& serialize_move_crc = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??5556574883EC30498BC0488BFA488BF1488B09F6C103"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??5556574883EC30498BC0488BFA488BF1488B09F6C103");

	const ::protection::addresses::address_t& service_read = ADDRESS_IMPL(
		::protection::addresses::hash("filesystem_stdio.dll:00488907488D05*????????488987E0000000~"),
		::protection::addresses::address_type::pattern,
		"filesystem_stdio.dll:00488907488D05*????????488987E0000000~");

	const ::protection::addresses::address_t& set_info = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:40554157488D6C24??4881EC????????4533FF"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:40554157488D6C24??4881EC????????4533FF");

	const ::protection::addresses::address_t& set_player_model = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D15????????488BCB>E8????????488BD7488BCB"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D15????????488BCB>E8????????488BD7488BCB");

	const ::protection::addresses::address_t& set_postprocess_vec = ADDRESS_IMPL(
		::protection::addresses::hash("engine2.dll:>E8????????440F289424"),
		::protection::addresses::address_type::pattern,
		"engine2.dll:>E8????????440F289424");

	// Обновлено после апдейта 2026-09-26.
	// Старый вид `>E8????????0FB64325` резолвил ЦЕЛЬ вызова, но хвост
	// `movzx eax,[rbx+0x25]` после вызова исчез, и сигнал потерялся.
	// Теперь якоримся на САМУ функцию: это __m128i-перегрузка
	// set_shader_param(map,hash,value) -- хеш-таблица из 4-х бакетов
	// (cmp r9d,2; cmp edx,1), пролог совпадает с пятью сиблингами,
	// различие только в хвостовом `cmp edx,1` и цели lea r10.
	// Хук ставится на вход -- эквивалентно прежнему резолву.
	const ::protection::addresses::address_t& set_shader_param = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48896C24104889742418574883EC20660F6ECA498BF0660F70C9008BEA488BF94533C9488BC166660F1F840000000000660F6FC14C8D15????????660F76000F50C885C9754841FFC14883C0104183F90272DD488B87A80000004885C0747690488D481033D266660F1F840000000000660F6FC1660F7601440F50C04585C07535FFC24883C11083FA0172"),
		::protection::addresses::address_type::pattern,
		"client.dll:48896C24104889742418574883EC20660F6ECA498BF0660F70C9008BEA488BF94533C9488BC166660F1F840000000000660F6FC14C8D15????????660F76000F50C885C9754841FFC14883C0104183F90272DD488B87A80000004885C0747690488D481033D266660F1F840000000000660F6FC1660F7601440F50C04585C07535FFC24883C11083FA0172");

	const ::protection::addresses::address_t& set_shader_param_i = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48896C24??48897424??574883EC20660F6ECA418BF0"),
		::protection::addresses::address_type::pattern,
		"client.dll:48896C24??48897424??574883EC20660F6ECA418BF0");

	const ::protection::addresses::address_t& set_view_angles = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:85D275??486381"),
		::protection::addresses::address_type::pattern,
		"client.dll:85D275??486381");

	// Обновлено после апдейта 2026-09-26.
	// Структура-хозяин уменьшилась: байтовый offset голосовой строки переехал
	// с 0x14C8 на 0xD8, поэтому прежний хвост `cmp [rbp+0x14C8],r14` исчез.
	// Проверка после вызова voice-сеттера теперь `cmp qword ptr [rbp+0xD8],r14;
	// je +0x13` -- уникальна (одно совпадение, цель RVA 0x19C07D0).
	// Функция принимает (rcx=voice, rdx=const char*, r8d=len(0xFFFFFFFF),
	// r9=flags) и собирает/присваивает строку в HUD-элемент голоса -- ровно
	// то, чем пользуется chat_print в impacts.cpp.
	const ::protection::addresses::address_t& set_voice_data = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4C39B5D8000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4C39B5D8000000");

	const ::protection::addresses::address_t& simulation_player = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4C3905*????????400F94C5"),
		::protection::addresses::address_type::pattern,
		"client.dll:4C3905*????????400F94C5");

	const ::protection::addresses::address_t& sort_primitives = ADDRESS_IMPL(
		::protection::addresses::hash("scenesystem.dll:4585C90F84????????5556574883EC30"),
		::protection::addresses::address_type::pattern,
		"scenesystem.dll:4585C90F84????????5556574883EC30");

	const ::protection::addresses::address_t& setup_fog = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??48896C24??48897424??48894C24??5741544155415641574883EC20486302"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??48896C24??48897424??48894C24??5741544155415641574883EC20486302");

	const ::protection::addresses::address_t& play_sound = ADDRESS_IMPL(
		::protection::addresses::hash("soundsystem.dll:4C8BDC55415541564157"),
		::protection::addresses::address_type::pattern,
		"soundsystem.dll:4C8BDC55415541564157");

	const ::protection::addresses::address_t& string_copy = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????0F104588"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????0F104588");

	const ::protection::addresses::address_t& subtick_move_alloc = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488B54CA088D4101894708EB16488B0F>E8????????488BD0488BCF"),
		::protection::addresses::address_type::pattern,
		"client.dll:488B54CA088D4101894708EB16488B0F>E8????????488BD0488BCF");

	const ::protection::addresses::address_t& trace_bullet = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:40535741564883EC508B8424"),
		::protection::addresses::address_type::pattern,
		"client.dll:40535741564883EC508B8424");

	// Обновлено после апдейта 2026-09-26.
	// Старый вид был вайлдкард-тяжёлым (`48895C24??48896C24??48897424??574156
	// 41574883EC??F20F1002`) и совпадал с ДВУМЯ функциями: 0x883B00 и
	// 0x18B7130 (обе -- почленное копирование с `movsd xmm0,[rdx]`). Развёл по
	// сайтам вызова: у 0x883B00 ровно один вызывающий -- RVA 0x8889D5, и он
	// воспроизводит использование из tracing.cpp (`lea rcx,[rbp+0xaf0]`
	// -- trace_data огромного размера, `mov rdx,rbx` -- start,
	// `lea r8,[rsp+0x78]` -- delta, `lea r9,[rbp+0x200]` -- filter,
	// `mov [rsp+20],r12d` -- penetration_count, `mov byte [rsp+28],1` --
	// trace_world). У 0x18B7130 пять вызывающих, и все -- контейнерные
	// memcpy-циклы. Якорь -- `lea r14,[r9+8]` (4-й аргумент) и
	// `movsd [rcx+0x1CF8]` (поле trace_data невероятного размера):
	// всё это делает шаблон уникальным.
	const ::protection::addresses::address_t& trace_bullet_data_init = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C240848896C2410488974241857415641574883EC40F20F10024D8D7108"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C240848896C2410488974241857415641574883EC40F20F10024D8D7108");

	// Обновлено после апдейта 2026-09-26.
	// Прежний вид `>E8????????3BDE744E` резолвил ЦЕЛЬ вызова по хвосту
	// `cmp ebx,esi; je +0x4E` -- сайт вызова компилятор пересобрал, и такого
	// `call` в образе больше нет. Функция (RVA 0x1775874, .pdata
	// 0x1775874-0x1775928, 180 байт) при этом на месте -- якоримся на НЕЁ:
	// это сброс `result` в состояние "нет попадания" -- обнуляет surface/
	// hit_entity/hitbox_data, пишет contents=-1 (`mov eax,-1` -> [rbp+0xb0])
	// и fraction=1.0f (`mov dword [rbp+0xAC],0x3F800000`). Оба поля совпадают
	// с теми, что пишет финализатор trace_bullet_update (0x886250),
	// а сами смещения -- с раскладкой result. Прямых вызывающих у функции
	// нет (диспетчеризуется через таблицу указателей), поэтому шаблон
	// начинается с тела: `mov r15,r13; mov [rip+?],r13; mov rax,[rip+?]`.
	// Две rip-диспозиции завайлдкардены -- одно совпадение.
	const ::protection::addresses::address_t& trace_bullet_free = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4D8BFD4C892D????????488B05????????4C89AD900000004489AD9800000048894500B8FFFFFFFF668985B8000000C785AC0000000000803F"),
		::protection::addresses::address_type::pattern,
		"client.dll:4D8BFD4C892D????????488B05????????4C89AD900000004489AD9800000048894500B8FFFFFFFF668985B8000000C785AC0000000000803F");

	// Обновлено после апдейта 2026-09-26.
	// Хвост `movss xmm12,[rbp+0x74]` сменил регистр на xmm11, а прежний якорь
	// `>E8` потерялся. Якоримся на саму функцию финализации трейса: пролог
	// (rbx/rbp/rsi в теневых слотах, frame 0x80) + сохранение xmm6 + четыре
	// аргумента (rcx, rdx, xmm2, r9) -- единственное совпадение.
	const ::protection::addresses::address_t& trace_bullet_update = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C240848896C24104889742418574881EC80000000488BE90F29742470488BCA498BF90F28F2488BDA"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C240848896C24104889742418574881EC80000000488BE90F29742470488BCA498BF90F28F2488BDA");

	const ::protection::addresses::address_t& trace_filter_init = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??48897424??574883EC??0FB641??33FF24"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??48897424??574883EC??0FB641??33FF24");

	const ::protection::addresses::address_t& trace_filter_set_collision = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:000041B800010000>E8????????4C397F38"),
		::protection::addresses::address_type::pattern,
		"client.dll:000041B800010000>E8????????4C397F38");

	// Обновлено после апдейта 2026-09-26.
	// Прежний вид резолвил ЦЕЛЬ вызова, хвост `comiss xmm6,[rbp+0x4C]` исчез.
	// trace_hull -- трассировка капсулой: принимает (ctx, result, start, end,
	// bbox, filter), делает slab-тест AABB (comiss [rdi], [rdi+4], [rdi+8]...).
	// Пролог с frame `lea rbp,[rsp-0x20]`/`sub rsp,0x1A0` и r14 из [rbp+0xF0]
	// -- одно совпадение; все 27 вызовов идут с шестью аргументами и bbox.
	const ::protection::addresses::address_t& trace_hull = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48897424185557415441554156488DAC2460FFFFFF4881ECA00100004C8BB5F0000000498BF14D8BE84C8BE2488BF94D"),
		::protection::addresses::address_type::pattern,
		"client.dll:48897424185557415441554156488DAC2460FFFFFF4881ECA00100004C8BB5F0000000498BF14D8BE84C8BE2488BF94D");

	const ::protection::addresses::address_t& trace_ray = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895424??48894C24??5553565741564157488DAC24????????4881EC"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895424??48894C24??5553565741564157488DAC24????????4881EC");

	// Обновлено после апдейта 2026-09-26.
	// Прежний сайт (`mov [rsp+0x20],rdi; call; mov rdi,[rip+?]`) разобран.
	// trace_ray_entity -- entity-вариант трасса из той же TU, что и рабочий
	// trace_ray (0x1078555, frame `lea rbp,[rax-0x118]; sub rsp,0x208`).
	// Здесь frame `lea rbp,[rax-0x118]; sub rsp,0x208` в форме r11-пролога
	// (`mov [rax+0x10],rdx`) -- одно совпадение.
	const ::protection::addresses::address_t& trace_ray_entity = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488BC4488950105553488DA8E8FEFFFF4881EC0802000048"),
		::protection::addresses::address_type::pattern,
		"client.dll:488BC4488950105553488DA8E8FEFFFF4881EC0802000048");

	// Обновлено после апдейта 2026-09-26.
	// Старый вид обрывался на `mov rsi,[rcx+?]; mov rcx,rsi`; компилятор
	// вставил между ними глобальный `mov rax,[rip+?]`. Берём пролог целиком
	// (frame 0x20, rbp/rsi/rbx в теневых слотах) -- это одно совпадение.
	const ::protection::addresses::address_t& update_fov_sensitivity = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C240848896C24104889742418574883EC20488BB980000000488BD94863"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C240848896C24104889742418574883EC20488BB980000000488BD94863");

	const ::protection::addresses::address_t& utl_vector_push = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????4C8BD0458B4A10"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????4C8BD0458B4A10");

	const ::protection::addresses::address_t& view_matrix = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D0D*????????48C1E006"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D0D*????????48C1E006");

	// viewmodel_update_mesh удалён после апдейта 2026-09-26: сигнатура
	// перестала находиться, а сама address_t больше нигде не используется
	// (ни как хук, ни через memory::call) -- мёртвое объявление.

	const ::protection::addresses::address_t& weapon_calculate_spread = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:28F3440F11442420>E8????????488D85B0000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:28F3440F11442420>E8????????488D85B0000000");

	const ::protection::addresses::address_t& weapon_get_entity_index = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????448B4500"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????448B4500");

	const ::protection::addresses::address_t& weapon_get_model_path = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C2410564883EC20488B1D????????"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C2410564883EC20488B1D????????");

	const ::protection::addresses::address_t& weapon_get_recoil_offset = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:>E8????????488D44243C"),
		::protection::addresses::address_type::pattern,
		"client.dll:>E8????????488D44243C");

	const ::protection::addresses::address_t& weapon_get_viewmodel = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:40534883EC20488BD9E8????????4883BB8803000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:40534883EC20488BD9E8????????4883BB8803000000");

	const ::protection::addresses::address_t& weapon_recoil_data = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:488D0D*????????488D8424????????41B801000000"),
		::protection::addresses::address_type::pattern,
		"client.dll:488D0D*????????488D8424????????41B801000000");

	const ::protection::addresses::address_t& weapon_set_mesh_group_mask = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??48897424??574883EC??488D99????????488B71"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??48897424??574883EC??488D99????????488B71");

	const ::protection::addresses::address_t& weapon_update_accuracy = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:405741564883EC68488BF9E8????????4C8BF04885C0"),
		::protection::addresses::address_type::pattern,
		"client.dll:405741564883EC68488BF9E8????????4C8BF04885C0");

	const ::protection::addresses::address_t& weapon_update_composite_material = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C241048896C2418488974242057415641574883EC20440FB6F2488BF9"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C241048896C2418488974242057415641574883EC20440FB6F2488BF9");

	// weapon_update_mesh удалён после апдейта 2026-09-26: сигнатура
	// перестала находиться, а address_t нигде не используется -- мёртвое
	// объявление (см. также viewmodel_update_mesh выше).

	// Обновлено после апдейта 2026-09-26.
	// Прежний хвост `mov [rbp-2E8h],rax` переставлен; пролог (пять
	// callee-saved push + frame 0x300) по-прежнему уникален, просто к нему
	// добавился `mov rax,[rip+?]`. Паттерн с этим глобалом -- одно совпадение.
	const ::protection::addresses::address_t& weapon_update_skin = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:4055534157488DAC2400FEFFFF4881EC00030000488B05????????440FB6FA48"),
		::protection::addresses::address_type::pattern,
		"client.dll:4055534157488DAC2400FEFFFF4881EC00030000488B05????????440FB6FA48");

	const ::protection::addresses::address_t& econ_item_view_set_attribute = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:40534883EC20488BD94881C108020000"),
		::protection::addresses::address_type::pattern,
		"client.dll:40534883EC20488BD94881C108020000");

	const ::protection::addresses::address_t& econ_item_view_remove_attribute = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:40534883EC20486381????????440FB7CA"),
		::protection::addresses::address_type::pattern,
		"client.dll:40534883EC20486381????????440FB7CA");

	const ::protection::addresses::address_t& econ_item_view_invalidate_description = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:48895C24??48897424??574883EC20488DB9????????488BF1"),
		::protection::addresses::address_type::pattern,
		"client.dll:48895C24??48897424??574883EC20488DB9????????488BF1");

	const ::protection::addresses::address_t& set_bodygroup = ADDRESS_IMPL(
		::protection::addresses::hash("client.dll:85D20F88????????555657"),
		::protection::addresses::address_type::pattern,
		"client.dll:85D20F88????????555657");

} // namespace patterns
