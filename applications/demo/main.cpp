#include "GEK\Utility\Trace.h"
#include "GEK\Utility\Display.h"
#include "GEK\Utility\FileSystem.h"
#include "GEK\Utility\String.h"
#include "GEK\Utility\Evaluator.h"
#include "GEK\Utility\XML.h"
#include "GEK\Context\Context.h"
#include "GEK\Engine\Engine.h"
#include <CommCtrl.h>
#include "resource.h"

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    case WM_INITDIALOG:
    {
        UINT32 width = 800;
        UINT32 height = 600;
        bool fullscreen = false;

        Gek::XmlDocument xmlDocument(Gek::XmlDocument::load(L"$root\\config.xml"));

        Gek::XmlNode xmlConfigNode = xmlDocument.getRoot();
        if (xmlConfigNode && xmlConfigNode.getType().CompareNoCase(L"config") == 0 && xmlConfigNode.hasChildElement(L"display"))
        {
            Gek::XmlNode xmlDisplayNode = xmlConfigNode.firstChildElement(L"display");
            if (xmlDisplayNode)
            {
                if (xmlDisplayNode.hasAttribute(L"width"))
                {
                    width = Gek::String::to<UINT32>(xmlDisplayNode.getAttribute(L"width"));
                }

                if (xmlDisplayNode.hasAttribute(L"height"))
                {
                    height = Gek::String::to<UINT32>(xmlDisplayNode.getAttribute(L"height"));
                }

                if (xmlDisplayNode.hasAttribute(L"fullscreen"))
                {
                    fullscreen = Gek::String::to<bool>(xmlDisplayNode.getAttribute(L"fullscreen"));
                }
            }
        }

        UINT32 selectIndex = 0;
        SendDlgItemMessage(dialog, IDC_MODES, CB_RESETCONTENT, 0, 0);
        std::vector<Gek::DisplayMode> modeList = Gek::getDisplayModes()[32];
        for (auto &mode : modeList)
        {
            CStringW aspectRatio(L"");
            switch (mode.aspectRatio)
            {
            case Gek::AspectRatio::_4x3:
                aspectRatio = L", (4x3)";
                break;

            case Gek::AspectRatio::_16x9:
                aspectRatio = L", (16x9)";
                break;

            case Gek::AspectRatio::_16x10:
                aspectRatio = L", (16x10)";
                break;
            };

            CStringW modeString;
            modeString.Format(L"%dx%%", mode.width, mode.height, aspectRatio.GetString());
            int modeIndex = SendDlgItemMessage(dialog, IDC_MODES, CB_ADDSTRING, 0, (WPARAM)modeString.GetString());
            if (mode.width == width && mode.height == height)
            {
                selectIndex = modeIndex;
            }
        }

        SendDlgItemMessage(dialog, IDC_FULLSCREEN, BM_SETCHECK, fullscreen ? BST_CHECKED : BST_UNCHECKED, 0);

        SendDlgItemMessage(dialog, IDC_MODES, CB_SETMINVISIBLE, 5, 0);
        SendDlgItemMessage(dialog, IDC_MODES, CB_SETEXTENDEDUI, TRUE, 0);
        SendDlgItemMessage(dialog, IDC_MODES, CB_SETCURSEL, selectIndex, 0);
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            std::vector<Gek::DisplayMode> modeList = Gek::getDisplayModes()[32];
            UINT32 selectIndex = SendDlgItemMessage(dialog, IDC_MODES, CB_GETCURSEL, 0, 0);
            auto &mode = modeList[selectIndex];

            Gek::XmlDocument xmlDocument(Gek::XmlDocument::load(L"$root\\config.xml"));

            Gek::XmlNode xmlConfigNode = xmlDocument.getRoot();
            if (!xmlConfigNode || xmlConfigNode.getType().CompareNoCase(L"config") != 0)
            {
                xmlDocument.create(L"config");
                xmlConfigNode = xmlDocument.getRoot();
            }

            Gek::XmlNode xmlDisplayNode = xmlConfigNode.firstChildElement(L"display");
            if (!xmlDisplayNode)
            {
                xmlDisplayNode = xmlConfigNode.createChildElement(L"display");
            }

            xmlDisplayNode.setAttribute(L"width", L"%", mode.width);
            xmlDisplayNode.setAttribute(L"height", L"%", mode.height);
            xmlDisplayNode.setAttribute(L"fullscreen", L"%", (SendDlgItemMessage(dialog, IDC_FULLSCREEN, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"true" : L"false"));
            xmlDocument.save(L"$root\\config.xml");

            EndDialog(dialog, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        };

        return TRUE;
    };

    return FALSE;
}

LRESULT CALLBACK WindowProc(HWND window, UINT32 message, WPARAM wParam, LPARAM lParam)
{
    LRESULT resultValue = 0;
    Gek::Engine *engineCore = reinterpret_cast<Gek::Engine *>(GetWindowLongPtr(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_CLOSE:
    case WM_DESTROY:
        engineCore->windowEvent(message, wParam, lParam);
        PostQuitMessage(0);
        break;

    default:
        if (engineCore)
        {
            resultValue = engineCore->windowEvent(message, wParam, lParam);
        }

        break;
    };

    if (resultValue == 0)
    {
        resultValue = DefWindowProc(window, message, wParam, lParam);
    }

    return resultValue;
}

int CALLBACK wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ wchar_t *strCommandLine, _In_ int nCmdShow)
{
    static const const wchar_t *materialFormat = \
        L"<?xml version=\"1.0\"?>\r\n" \
        L"<material>\r\n" \
        L"    <shader>standard</shader>\r\n" \
        L"    <maps>\r\n" \
        L"        <albedo>schulz/old_tiles/old_tiles_a</albedo>\r\n" \
        L"        <normal>schulz/old_tiles/old_tiles_n</normal>\r\n" \
        L"        <roughness>*color:%f</roughness>\r\n" \
        L"        <metalness>*color:%f</metalness>\r\n" \
        L"    </maps>\r\n" \
        L"</material>";

    static const const wchar_t *entityFormat = \
        L"        <entity>\r\n" \
        L"            <transform position=\"(%f,2.0,%f)\" scale=\"(0.5,0.5,0.5)\" />\r\n" \
        L"            <!--color>lerp(.5,1,arand(1)),lerp(.5,1,arand(1)),lerp(.5,1,arand(1)),1</color-->\r\n" \
        L"            <shape skin=\"debug//r%d_m%\" parameters=\"1\">sphere</shape>\r\n" \
        L"        </entity>\r\n";

    static const const wchar_t *materialLibraryFormat = \
        L"    <effect id=\"debug_%name%\">\r\n" \
        L"      <profile_COMMON>\r\n" \
        L"        <newparam sid=\"%name%_png-surface\">\r\n" \
        L"          <surface type=\"2D\">\r\n" \
        L"            <init_from>%name%_png</init_from>\r\n" \
        L"          </surface>\r\n" \
        L"        </newparam>\r\n" \
        L"        <newparam sid=\"%name%_png-sampler\">\r\n" \
        L"          <sampler2D>\r\n" \
        L"            <source>%name%_png-surface</source>\r\n" \
        L"          </sampler2D>\r\n" \
        L"        </newparam>\r\n" \
        L"        <technique sid=\"common\">\r\n" \
        L"          <blinn>\r\n" \
        L"            <emission>\r\n" \
        L"              <color>0 0 0 1</color>\r\n" \
        L"            </emission>\r\n" \
        L"            <ambient>\r\n" \
        L"              <color>0.5882353 0.5882353 0.5882353 1</color>\r\n" \
        L"            </ambient>\r\n" \
        L"            <diffuse>\r\n" \
        L"              <texture texture=\"%name%_png-sampler\" texcoord=\"CHANNEL1\"/>\r\n" \
        L"            </diffuse>\r\n" \
        L"            <specular>\r\n" \
        L"              <color>0.9 0.9 0.9 1</color>\r\n" \
        L"            </specular>\r\n" \
        L"            <shininess>\r\n" \
        L"              <float>0</float>\r\n" \
        L"            </shininess>\r\n" \
        L"            <reflective>\r\n" \
        L"              <color>0 0 0 1</color>\r\n" \
        L"            </reflective>\r\n" \
        L"            <transparent opaque=\"A_ONE\">\r\n" \
        L"              <color>1 1 1 1</color>\r\n" \
        L"            </transparent>\r\n" \
        L"            <transparency>\r\n" \
        L"              <float>1</float>\r\n" \
        L"            </transparency>\r\n" \
        L"          </blinn>\r\n" \
        L"        </technique>\r\n" \
        L"      </profile_COMMON>\r\n" \
        L"      <extra>\r\n" \
        L"        <technique profile=\"OpenCOLLADA3dsMax\">\r\n" \
        L"          <extended_shader>\r\n" \
        L"            <apply_reflection_dimming>0</apply_reflection_dimming>\r\n" \
        L"            <dim_level>0</dim_level>\r\n" \
        L"            <falloff_type>0</falloff_type>\r\n" \
        L"            <index_of_refraction>1.5</index_of_refraction>\r\n" \
        L"            <opacity_type>0</opacity_type>\r\n" \
        L"            <reflection_level>3</reflection_level>\r\n" \
        L"            <wire_size>1</wire_size>\r\n" \
        L"            <wire_units>0</wire_units>\r\n" \
        L"          </extended_shader>\r\n" \
        L"          <shader>\r\n" \
        L"            <ambient_diffuse_lock>1</ambient_diffuse_lock>\r\n" \
        L"            <ambient_diffuse_texture_lock>1</ambient_diffuse_texture_lock>\r\n" \
        L"            <diffuse_specular_lock>0</diffuse_specular_lock>\r\n" \
        L"            <soften>0.1</soften>\r\n" \
        L"            <use_self_illum_color>0</use_self_illum_color>\r\n" \
        L"          </shader>\r\n" \
        L"        </technique>\r\n" \
        L"      </extra>\r\n" \
        L"    </effect>\r\n";

    static const const wchar_t *fileLibraryFormat = \
        L"    <image id=\"%name%_png\">\r\n" \
        L"      <init_from>../textures/debug/%name%.png</init_from>\r\n" \
        L"    </image>\r\n";

    CStringW entities;
    CStringW materialLibrary;
    CStringW fileLibrary;
    for (UINT32 roughness = 0; roughness < 10; roughness++)
    {
        for (UINT32 metalness = 0; metalness < 10; metalness++)
        {
            CStringW material(Gek::String::format(materialFormat, (float(roughness) / 9.0f), (float(metalness) / 9.0f)));
            CStringW fileName(Gek::String::format(L"r%d_m%", roughness, metalness));
            Gek::FileSystem::save((L"$root\\data\\materials\\debug\\" + fileName + L".xml"), material);

            entities += Gek::String::format(entityFormat, (float(roughness) - 4.5f), (float(metalness) - 4.5f), roughness, metalness);
            //CopyFile(Gek::FileSystem::expandPath(L"$root\\data\\textures\\debug\\r0_m0.png"), Gek::FileSystem::expandPath(L"$root\\data\\textures\\debug\\") + fileName + L".png", false);

            materialLibrary += [&](void) -> CStringW { CStringW data(materialLibraryFormat); data.Replace(L"%name%", fileName); return data; }();
            fileLibrary += [&](void) -> CStringW { CStringW data(fileLibraryFormat); data.Replace(L"%name%", fileName); return data; }();
        }
    }

    if (DialogBox(hInstance, MAKEINTRESOURCE(IDD_SETTINGS), nullptr, DialogProc) == IDOK)
    {
        //_clearfp();
        //unsigned unused_current_word = 0;
        //_controlfp_s(&unused_current_word, 0, _EM_ZERODIVIDE | _EM_INVALID);

        std::vector<std::wstring> searchPathList;

#ifdef _DEBUG
        SetCurrentDirectory(Gek::FileSystem::expandPath(L"$root\\Debug"));
        searchPathList.push_back(L"$root\\Debug\\Plugins");
#else
        SetCurrentDirectory(Gek::FileSystem::expandPath(L"$root\\Release"));
        searchPathList.push_back(L"$root\\Release\\Plugins");
#endif

        Gek::traceInitialize();
        Gek::ContextPtr context(Gek::Context::create(searchPathList));
        if (context)
        {
            WNDCLASS kClass;
            kClass.style = 0;
            kClass.lpfnWndProc = WindowProc;
            kClass.cbClsExtra = 0;
            kClass.cbWndExtra = 0;
            kClass.hInstance = GetModuleHandle(nullptr);
            kClass.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(103));
            kClass.hCursor = nullptr;
            kClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            kClass.lpszMenuName = nullptr;
            kClass.lpszClassName = L"GEKvX_Engine_Demo";
            if (RegisterClass(&kClass))
            {
                UINT32 width = 800;
                UINT32 height = 600;
                bool fullscreen = false;

                Gek::XmlDocument xmlDocument(Gek::XmlDocument::load(L"$root\\config.xml"));

                Gek::XmlNode xmlConfigNode = xmlDocument.getRoot();
                if (xmlConfigNode && xmlConfigNode.getType().CompareNoCase(L"config") == 0 && xmlConfigNode.hasChildElement(L"display"))
                {
                    Gek::XmlNode xmlDisplayNode = xmlConfigNode.firstChildElement(L"display");
                    if (xmlDisplayNode)
                    {
                        if (xmlDisplayNode.hasAttribute(L"width"))
                        {
                            width = Gek::String::to<UINT32>(xmlDisplayNode.getAttribute(L"width"));
                        }

                        if (xmlDisplayNode.hasAttribute(L"height"))
                        {
                            height = Gek::String::to<UINT32>(xmlDisplayNode.getAttribute(L"height"));
                        }

                        if (xmlDisplayNode.hasAttribute(L"fullscreen"))
                        {
                            fullscreen = Gek::String::to<bool>(xmlDisplayNode.getAttribute(L"fullscreen"));
                        }
                    }
                }

                RECT clientRect;
                clientRect.left = 0;
                clientRect.top = 0;
                clientRect.right = width;
                clientRect.bottom = height;
                AdjustWindowRect(&clientRect, WS_OVERLAPPEDWINDOW, false);
                int windowWidth = (clientRect.right - clientRect.left);
                int windowHeight = (clientRect.bottom - clientRect.top);
                int centerPositionX = (GetSystemMetrics(SM_CXFULLSCREEN) / 2) - ((clientRect.right - clientRect.left) / 2);
                int centerPositionY = (GetSystemMetrics(SM_CYFULLSCREEN) / 2) - ((clientRect.bottom - clientRect.top) / 2);
                HWND window = CreateWindow(L"GEKvX_Engine_Demo", L"GEKvX Engine - Demo", WS_SYSMENU | WS_BORDER | WS_MINIMIZEBOX, centerPositionX, centerPositionY, windowWidth, windowHeight, 0, nullptr, GetModuleHandle(nullptr), 0);
                if (window)
                {
                    Gek::EnginePtr engineCore(context->createClass<Gek::Engine>(L"GEKEngine", window));
                    if (engineCore)
                    {
                        SetWindowLongPtr(window, GWLP_USERDATA, LONG(engineCore.get()));
                        ShowWindow(window, SW_SHOW);
                        UpdateWindow(window);

                        MSG message = { 0 };
                        while (message.message != WM_QUIT)
                        {
                            while (PeekMessage(&message, nullptr, 0U, 0U, PM_REMOVE))
                            {
                                TranslateMessage(&message);
                                DispatchMessage(&message);
                            };

                            if (!engineCore->update())
                            {
                                break;
                            }
                        };

                        DestroyWindow(window);
                        SetWindowLongPtr(window, GWLP_USERDATA, 0);
                    }
                    else
                    {
                        DWORD error = GetLastError();
                        GEK_TRACE_EVENT("Unable to create window", GEK_PARAMETER(error));
                    }
                }
                else
                {
                    DWORD error = GetLastError();
                    GEK_TRACE_EVENT("Unable to register window class", GEK_PARAMETER(error));
                }
            }

            Gek::traceShutDown();
        }

        return 0;
    }

    return -1;
}