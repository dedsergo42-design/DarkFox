#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>
#include <functional>

namespace UI {

	// Color palette definition for Neverlose-inspired Dark Theme
	namespace Colors {
		constexpr ImVec4 MainBackground     = ImVec4( 0.047f, 0.047f, 0.047f, 1.000f ); // #0C0C0C
		constexpr ImVec4 ChildBackground    = ImVec4( 0.102f, 0.102f, 0.102f, 1.000f ); // #1A1A1A
		constexpr ImVec4 SidebarBackground  = ImVec4( 0.071f, 0.071f, 0.071f, 1.000f ); // #121212
		constexpr ImVec4 CardBackground     = ImVec4( 0.118f, 0.118f, 0.118f, 1.000f ); // #1E1E1E
		constexpr ImVec4 CardHovered        = ImVec4( 0.145f, 0.145f, 0.145f, 1.000f ); // #252525
		
		constexpr ImVec4 TextPrimary        = ImVec4( 1.000f, 1.000f, 1.000f, 1.000f ); // #FFFFFF
		constexpr ImVec4 TextSecondary      = ImVec4( 0.627f, 0.627f, 0.627f, 1.000f ); // #A0A0A0
		constexpr ImVec4 TextDisabled       = ImVec4( 0.380f, 0.380f, 0.380f, 1.000f ); // #616161

		constexpr ImVec4 PurpleAccent       = ImVec4( 0.690f, 0.149f, 1.000f, 1.000f ); // #B026FF
		constexpr ImVec4 YellowAccent       = ImVec4( 1.000f, 0.784f, 0.000f, 1.000f ); // #FFC800
		constexpr ImVec4 YellowAccentHover  = ImVec4( 1.000f, 0.835f, 0.200f, 1.000f ); // #FFD533

		constexpr ImVec4 BorderColor        = ImVec4( 1.000f, 1.000f, 1.000f, 0.050f ); // rgba(255,255,255,0.05)
		constexpr ImVec4 BorderActive       = ImVec4( 0.690f, 0.149f, 1.000f, 0.400f );
	}

	class Theme {
	public:
		// Primary function to apply modern flat dark styling to ImGui
		static void ApplyStyle() {
			ImGuiStyle& style = ImGui::GetStyle();
			ImVec4* colors = style.Colors;

			// 1. Style variables & Rounding
			style.WindowRounding    = 10.0f; // Soft rounded main window
			style.ChildRounding     = 8.0f;  // Rounded cards and panels
			style.FrameRounding     = 5.0f;  // Inputs, buttons
			style.PopupRounding     = 6.0f;
			style.ScrollbarRounding = 4.0f;
			style.GrabRounding      = 4.0f;  // Sliders
			style.TabRounding       = 6.0f;

			style.WindowBorderSize  = 0.0f;  // Flat design, no heavy 3D borders
			style.ChildBorderSize   = 1.0f;
			style.FrameBorderSize   = 1.0f;
			style.PopupBorderSize   = 1.0f;
			style.TabBorderSize     = 0.0f;

			style.WindowPadding     = ImVec2( 16.0f, 16.0f );
			style.FramePadding      = ImVec2( 10.0f, 8.0f );
			style.ItemSpacing       = ImVec2( 12.0f, 10.0f );
			style.ItemInnerSpacing  = ImVec2( 8.0f, 6.0f );
			style.TouchExtraPadding = ImVec2( 0.0f, 0.0f );
			style.IndentSpacing     = 20.0f;
			style.ScrollbarSize     = 8.0f;

			// 2. Color Palette Override
			colors[ImGuiCol_Text]                  = Colors::TextPrimary;
			colors[ImGuiCol_TextDisabled]          = Colors::TextDisabled;
			colors[ImGuiCol_WindowBg]              = Colors::MainBackground;
			colors[ImGuiCol_ChildBg]               = Colors::ChildBackground;
			colors[ImGuiCol_PopupBg]               = Colors::ChildBackground;
			colors[ImGuiCol_Border]                = Colors::BorderColor;
			colors[ImGuiCol_BorderShadow]          = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
			colors[ImGuiCol_FrameBg]               = ImVec4( 0.08f, 0.08f, 0.08f, 1.00f );
			colors[ImGuiCol_FrameBgHovered]        = ImVec4( 0.14f, 0.14f, 0.14f, 1.00f );
			colors[ImGuiCol_FrameBgActive]         = ImVec4( 0.18f, 0.18f, 0.18f, 1.00f );
			colors[ImGuiCol_TitleBg]               = Colors::MainBackground;
			colors[ImGuiCol_TitleBgActive]         = Colors::MainBackground;
			colors[ImGuiCol_TitleBgCollapsed]      = Colors::MainBackground;
			colors[ImGuiCol_MenuBarBg]             = Colors::MainBackground;
			colors[ImGuiCol_ScrollbarBg]           = Colors::MainBackground;
			colors[ImGuiCol_ScrollbarGrab]         = ImVec4( 0.25f, 0.25f, 0.25f, 1.00f );
			colors[ImGuiCol_ScrollbarGrabHovered]  = Colors::PurpleAccent;
			colors[ImGuiCol_ScrollbarGrabActive]   = Colors::PurpleAccent;
			colors[ImGuiCol_CheckMark]             = Colors::PurpleAccent;
			colors[ImGuiCol_SliderGrab]            = Colors::PurpleAccent;
			colors[ImGuiCol_SliderGrabActive]      = Colors::PurpleAccent;
			colors[ImGuiCol_Button]                = ImVec4( 0.14f, 0.14f, 0.14f, 1.00f );
			colors[ImGuiCol_ButtonHovered]         = ImVec4( 0.20f, 0.20f, 0.20f, 1.00f );
			colors[ImGuiCol_ButtonActive]          = Colors::PurpleAccent;
			colors[ImGuiCol_Header]                = ImVec4( 0.16f, 0.16f, 0.16f, 1.00f );
			colors[ImGuiCol_HeaderHovered]         = ImVec4( 0.22f, 0.22f, 0.22f, 1.00f );
			colors[ImGuiCol_HeaderActive]          = Colors::PurpleAccent;
			colors[ImGuiCol_Separator]             = Colors::BorderColor;
			colors[ImGuiCol_SeparatorHovered]      = Colors::PurpleAccent;
			colors[ImGuiCol_SeparatorActive]       = Colors::PurpleAccent;
			colors[ImGuiCol_ResizeGrip]            = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
			colors[ImGuiCol_Tab]                   = Colors::ChildBackground;
			colors[ImGuiCol_TabHovered]            = ImVec4( 0.20f, 0.20f, 0.20f, 1.00f );
			colors[ImGuiCol_TabActive]             = Colors::YellowAccent;
			colors[ImGuiCol_TabUnfocused]          = Colors::ChildBackground;
			colors[ImGuiCol_TabUnfocusedActive]    = Colors::YellowAccent;
			colors[ImGuiCol_PlotLines]             = Colors::PurpleAccent;
			colors[ImGuiCol_PlotLinesHovered]      = Colors::YellowAccent;
			colors[ImGuiCol_PlotHistogram]         = Colors::PurpleAccent;
			colors[ImGuiCol_PlotHistogramHovered]  = Colors::YellowAccent;
			colors[ImGuiCol_TableHeaderBg]         = Colors::ChildBackground;
			colors[ImGuiCol_TableBorderStrong]     = Colors::BorderColor;
			colors[ImGuiCol_TableBorderLight]      = ImVec4( 1.00f, 1.00f, 1.00f, 0.02f );
			colors[ImGuiCol_TableRowBg]            = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
			colors[ImGuiCol_TableRowBgAlt]         = ImVec4( 1.00f, 1.00f, 1.00f, 0.02f );
			colors[ImGuiCol_TextSelectedBg]        = ImVec4( 0.69f, 0.15f, 1.00f, 0.35f );
			colors[ImGuiCol_DragDropTarget]        = Colors::YellowAccent;
			colors[ImGuiCol_NavHighlight]          = Colors::PurpleAccent;
			colors[ImGuiCol_NavWindowingHighlight] = ImVec4( 1.00f, 1.00f, 1.00f, 0.70f );
			colors[ImGuiCol_NavWindowingDimBg]     = ImVec4( 0.00f, 0.00f, 0.00f, 0.60f );
			colors[ImGuiCol_ModalWindowDimBg]      = ImVec4( 0.00f, 0.00f, 0.00f, 0.75f );
		}
	};

	// Structure for Navigation Tab
	struct NavigationItem {
		std::string name;
		std::string icon; // FontAwesome or icon glyph
	};

	// Structure for Skin Item Card
	struct SkinCardItem {
		std::string name;
		std::string category;
		ImTextureID image_texture{ nullptr };
		ImU32 rarity_color_start{ IM_COL32( 220, 40, 40, 255 ) };  // Red top line
		ImU32 rarity_color_end{ IM_COL32( 15, 15, 15, 0 ) };      // Fade to transparent/dark
		bool selected{ false };
	};

	// Components renderer class
	class MenuRenderer {
	public:
		static void RenderSidebar( 
			float width, 
			int& active_tab, 
			const std::vector<NavigationItem>& tabs,
			const std::string& user_name = "panic",
			const std::string& user_role = "Development",
			ImTextureID avatar_tex = nullptr ) 
		{
			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			const ImVec2 pos = ImGui::GetCursorScreenPos();
			const float height = ImGui::GetContentRegionAvail().y;

			// Background panel for Sidebar (#121212)
			draw_list->AddRectFilled( 
				pos, 
				ImVec2( pos.x + width, pos.y + height ), 
				IM_COL32( 18, 18, 18, 255 ), 
				10.0f, 
				ImDrawFlags_RoundCornersLeft 
			);

			ImGui::BeginGroup();
			ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.0f, 8.0f ) );

			// 1. Logo / Title Section
			ImGui::SetCursorScreenPos( ImVec2( pos.x + 20.0f, pos.y + 24.0f ) );
			ImGui::PushFont( ImGui::GetIO().Fonts->Fonts[0] ); // Large/Bold font
			ImGui::TextColored( ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ), "NEVERLOSE" );
			ImGui::PopFont();

			ImGui::SetCursorScreenPos( ImVec2( pos.x + 20.0f, pos.y + 50.0f ) );
			ImGui::TextColored( Colors::TextSecondary, "v2.0 | CS2 Edition" );

			ImGui::SetCursorScreenPos( ImVec2( pos.x + 15.0f, pos.y + 80.0f ) );
			ImGui::Separator();

			// 2. Navigation Categories
			ImGui::SetCursorScreenPos( ImVec2( pos.x + 10.0f, pos.y + 95.0f ) );
			for ( int i = 0; i < static_cast<int>( tabs.size() ); ++i ) {
				const bool is_active = ( active_tab == i );
				const ImVec2 btn_pos = ImGui::GetCursorScreenPos();
				const float btn_w = width - 20.0f;
				const float btn_h = 42.0f;

				// Custom tab drawing
				const ImU32 bg_col = is_active 
					? IM_COL32( 176, 38, 255, 40 )   // Purple subtle fill for active
					: ( ImGui::IsMouseHoveringRect( btn_pos, ImVec2( btn_pos.x + btn_w, btn_pos.y + btn_h ) ) 
						? IM_COL32( 255, 255, 255, 10 ) 
						: IM_COL32( 0, 0, 0, 0 ) );

				draw_list->AddRectFilled( btn_pos, ImVec2( btn_pos.x + btn_w, btn_pos.y + btn_h ), bg_col, 6.0f );

				if ( is_active ) {
					// Active purple bar indicator on left side
					draw_list->AddRectFilled( 
						btn_pos, 
						ImVec2( btn_pos.x + 4.0f, btn_pos.y + btn_h ), 
						IM_COL32( 176, 38, 255, 255 ), 
						2.0f, 
						ImDrawFlags_RoundCornersLeft 
					);
				}

				ImGui::SetCursorScreenPos( ImVec2( btn_pos.x + 16.0f, btn_pos.y + 11.0f ) );
				const ImVec4 text_col = is_active ? Colors::PurpleAccent : Colors::TextSecondary;
				
				std::string label = tabs[i].icon.empty() ? tabs[i].name : ( tabs[i].icon + "   " + tabs[i].name );
				if ( ImGui::Selectable( label.c_str(), is_active, ImGuiSelectableFlags_None, ImVec2( btn_w, btn_h - 10.0f ) ) ) {
					active_tab = i;
				}

				ImGui::SetCursorScreenPos( ImVec2( btn_pos.x, btn_pos.y + btn_h + 4.0f ) );
			}

			// 3. User Profile Section at Bottom
			const float profile_y = pos.y + height - 64.0f;
			draw_list->AddLine( ImVec2( pos.x + 15.0f, profile_y - 10.0f ), ImVec2( pos.x + width - 15.0f, profile_y - 10.0f ), IM_COL32( 255, 255, 255, 12 ) );

			// Avatar Circle
			const ImVec2 avatar_center = ImVec2( pos.x + 36.0f, profile_y + 24.0f );
			const float avatar_radius = 18.0f;
			if ( avatar_tex ) {
				draw_list->AddImageRounded( avatar_tex, ImVec2( avatar_center.x - avatar_radius, avatar_center.y - avatar_radius ), ImVec2( avatar_center.x + avatar_radius, avatar_center.y + avatar_radius ), ImVec2( 0, 0 ), ImVec2( 1, 1 ), IM_COL32_WHITE, avatar_radius );
			} else {
				// Fallback colored avatar
				draw_list->AddCircleFilled( avatar_center, avatar_radius, IM_COL32( 176, 38, 255, 200 ) );
				draw_list->AddText( ImVec2( avatar_center.x - 5.0f, avatar_center.y - 7.0f ), IM_COL32_WHITE, "P" );
			}

			// User Nickname & Role Text
			ImGui::SetCursorScreenPos( ImVec2( pos.x + 64.0f, profile_y + 12.0f ) );
			ImGui::TextColored( Colors::TextPrimary, "%s", user_name.c_str() );
			
			ImGui::SetCursorScreenPos( ImVec2( pos.x + 64.0f, profile_y + 30.0f ) );
			ImGui::TextColored( Colors::YellowAccent, "%s", user_role.c_str() );

			ImGui::PopStyleVar();
			ImGui::EndGroup();
		}

		// Top Sub-Tabs (e.g. "T Skins" vs "CT Skins")
		static bool RenderCustomTab( const char* label, bool active, const ImVec2& size = ImVec2( 120.0f, 36.0f ) ) {
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			if ( window->SkipItems ) return false;

			const ImGuiID id = window->GetID( label );
			const ImVec2 pos = window->DC.CursorPos;
			const ImRect bb( pos, ImVec2( pos.x + size.x, pos.y + size.y ) );

			ImGui::ItemSize( bb );
			if ( !ImGui::ItemAdd( bb, id ) ) return false;

			bool hovered, held;
			bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );

			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			
			// Active: Bright Yellow (#FFC800) with Black Text. Inactive: Dark Frame with White Text.
			const ImU32 bg_col = active 
				? IM_COL32( 255, 200, 0, 255 ) 
				: ( hovered ? IM_COL32( 40, 40, 40, 255 ) : IM_COL32( 26, 26, 26, 255 ) );

			const ImU32 text_col = active ? IM_COL32( 10, 10, 10, 255 ) : IM_COL32( 220, 220, 220, 255 );

			draw_list->AddRectFilled( bb.Min, bb.Max, bg_col, 6.0f );
			
			const ImVec2 text_size = ImGui::CalcTextSize( label );
			const ImVec2 text_pos = ImVec2( bb.Min.x + ( size.x - text_size.x ) * 0.5f, bb.Min.y + ( size.y - text_size.y ) * 0.5f );
			draw_list->AddText( text_pos, text_col, label );

			return pressed;
		}

		// Custom Skin / Item Grid Card Component with Rarity Gradient
		static bool RenderSkinCard( const SkinCardItem& item, const ImVec2& card_size = ImVec2( 160.0f, 180.0f ) ) {
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			if ( window->SkipItems ) return false;

			const ImGuiID id = window->GetID( item.name.c_str() );
			const ImVec2 pos = window->DC.CursorPos;
			const ImRect bb( pos, ImVec2( pos.x + card_size.x, pos.y + card_size.y ) );

			ImGui::ItemSize( bb );
			if ( !ImGui::ItemAdd( bb, id ) ) return false;

			bool hovered, held;
			bool pressed = ImGui::ButtonBehavior( bb, id, &hovered, &held );

			ImDrawList* draw_list = ImGui::GetWindowDrawList();

			// Card Base Background (#1E1E1E)
			const ImU32 bg_col = item.selected 
				? IM_COL32( 35, 30, 45, 255 ) 
				: ( hovered ? IM_COL32( 37, 37, 37, 255 ) : IM_COL32( 30, 30, 30, 255 ) );

			draw_list->AddRectFilled( bb.Min, bb.Max, bg_col, 8.0f );

			// Top Rarity Gradient Bar (Red / Purple to Transparent/Dark)
			const float bar_height = 18.0f;
			draw_list->AddRectFilledMultiColor( 
				bb.Min, 
				ImVec2( bb.Max.x, bb.Min.y + bar_height ), 
				item.rarity_color_start, 
				item.rarity_color_start, 
				item.rarity_color_end, 
				item.rarity_color_end 
			);

			// Item Image Rendering (Centered)
			const float img_padding = 24.0f;
			const ImVec2 img_min( bb.Min.x + img_padding, bb.Min.y + 28.0f );
			const ImVec2 img_max( bb.Max.x - img_padding, bb.Min.y + 120.0f );

			if ( item.image_texture ) {
				draw_list->AddImage( item.image_texture, img_min, img_max );
			} else {
				// Placeholder image frame
				draw_list->AddRectFilled( img_min, img_max, IM_COL32( 20, 20, 20, 150 ), 4.0f );
				draw_list->AddText( ImVec2( img_min.x + 20.0f, img_min.y + 30.0f ), IM_COL32( 150, 150, 150, 255 ), "[WEAPON]" );
			}

			// Item Category & Title Label
			const ImVec2 name_size = ImGui::CalcTextSize( item.name.c_str() );
			const ImVec2 name_pos( bb.Min.x + ( card_size.x - name_size.x ) * 0.5f, bb.Max.y - 32.0f );
			draw_list->AddText( name_pos, IM_COL32_WHITE, item.name.c_str() );

			if ( !item.category.empty() ) {
				const ImVec2 cat_size = ImGui::CalcTextSize( item.category.c_str() );
				const ImVec2 cat_pos( bb.Min.x + ( card_size.x - cat_size.x ) * 0.5f, bb.Max.y - 18.0f );
				draw_list->AddText( cat_pos, IM_COL32( 140, 140, 140, 255 ), item.category.c_str() );
			}

			// Card Border Selection Highlight
			if ( item.selected ) {
				draw_list->AddRect( bb.Min, bb.Max, IM_COL32( 176, 38, 255, 255 ), 8.0f, 0, 2.0f );
			} else if ( hovered ) {
				draw_list->AddRect( bb.Min, bb.Max, IM_COL32( 255, 255, 255, 30 ), 8.0f, 0, 1.0f );
			}

			return pressed;
		}
	};
}
