#pragma once
#include "ImGui.h"
#include "imgui_markdown.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Shellapi.h"
#include <string>
#include <cstdlib>
inline ImGui::MarkdownImageData ImageCallback(ImGui::MarkdownLinkCallbackData data_)
{
    // In your application you would load an image based on data_ input. Here we just use the imgui font texture.
#ifdef IMGUI_HAS_TEXTURES // used to detect dynamic font capability
    ImTextureID image = ImGui::GetIO().Fonts->TexRef.GetTexID();
#else
    ImTextureID image = ImGui::GetIO().Fonts->TexID;
#endif
    // > C++14 can use ImGui::MarkdownImageData imageData{ true, false, image, ImVec2( 40.0f, 20.0f ) };
    ImGui::MarkdownImageData imageData;
    imageData.isValid = true;
    imageData.useLinkCallback = false;
    imageData.user_texture_id = image;
    imageData.size = ImVec2(40.0f, 20.0f);

    // For image resize when available size.x > image width, add
    ImVec2 const contentSize = ImGui::GetContentRegionAvail();
    if (imageData.size.x > contentSize.x)
    {
        float const ratio = imageData.size.y / imageData.size.x;
        imageData.size.x = contentSize.x;
        imageData.size.y = contentSize.x * ratio;
    }

    return imageData;
}
class MarkDown
{
public:
	MarkDown(float font_size) : m_fontSize(font_size) {};
	~MarkDown() {};

    void Markdown(const std::string& markdown_)
    {
        // You can make your own Markdown function with your prefered string container and markdown config.
        // > C++14 can use ImGui::MarkdownConfig mdConfig{ LinkCallback, NULL, ImageCallback, ICON_FA_LINK, { { H1, true }, { H2, true }, { H3, false } }, NULL };
        m_mdConfig.linkCallback = LinkCallback;
        m_mdConfig.tooltipCallback = NULL;
        m_mdConfig.imageCallback = ImageCallback;
        //m_mdConfig.linkIcon = ICON_FA_LINK;
#ifdef IMGUI_HAS_TEXTURES // used to detect dynamic font capability
        m_mdConfig.headingFormats[0] = { H1, true,  m_fontSize * 1.4f };
        m_mdConfig.headingFormats[1] = { H2, true,  m_fontSize * 1.3f };
        m_mdConfig.headingFormats[2] = { H3, true,  m_fontSize * 1.2f };
#else
        m_mdConfig.headingFormats[0] = { H1, true };
        m_mdConfig.headingFormats[1] = { H2, true };
        m_mdConfig.headingFormats[2] = { H3, false };
#endif

        m_mdConfig.userData = NULL;
        m_mdConfig.formatCallback = ExampleMarkdownFormatCallback;
        ImGui::Markdown(markdown_.c_str(), markdown_.length(), m_mdConfig);
    }

    void LoadFonts(std::string path)
    {
        ImGuiIO& io = ImGui::GetIO();
        //io.Fonts->Clear();
        //// Base font
        //io.Fonts->AddFontFromFileTTF("c:/windows/fonts/simhei.ttf", m_fontSize);
        // Bold headings H2 and H3
        H2 = io.Fonts->AddFontFromFileTTF(path.c_str(), m_fontSize);
        H3 = H2;
        // bold heading H1
#ifdef IMGUI_HAS_TEXTURES // used to detect dynamic font capability
        H1 = H2; // size can be set in headingFormats
#else
        float fontSizeH1 = fontSize * 1.1f;
        H1 = io.Fonts->AddFontFromFileTTF("myfont-bold.ttf", fontSizeH1);
#endif

    }

private:
    static void LinkCallback(ImGui::MarkdownLinkCallbackData data_)
    {
        const std::string openCmd = "start \"\" ";
        std::string url(data_.link, data_.linkLength);
        if (!data_.isImage)
        {
            if (url == "./doc/help.pdf") {
                std::string command = openCmd + "\"" + url + "\"";
                int result = std::system(command.c_str());
                if (result != 0) {
                    fprintf(stderr, "Failed to open PDF file");
                }
                return;
            }
            ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
    }

    static void ExampleMarkdownFormatCallback(const ImGui::MarkdownFormatInfo& markdownFormatInfo_, bool start_)
    {
        // Call the default first so any settings can be overwritten by our implementation.
        // Alternatively could be called or not called in a switch statement on a case by case basis.
        // See defaultMarkdownFormatCallback definition for furhter examples of how to use it.
        ImGui::defaultMarkdownFormatCallback(markdownFormatInfo_, start_);

        switch (markdownFormatInfo_.type)
        {
            // example: change the colour of heading level 2
        case ImGui::MarkdownFormatType::HEADING:
        {
            if (markdownFormatInfo_.level == 0)
            {
                if (start_)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                }
                else
                {
                    ImGui::PopStyleColor();
                }
            }
            break;
        }
        default:
        {
            break;
        }
        }
    }

private:
    ImFont* H1 = NULL;
    ImFont* H2 = NULL;
    ImFont* H3 = NULL;
	ImGui::MarkdownConfig m_mdConfig;
    float m_fontSize = 13;
};
