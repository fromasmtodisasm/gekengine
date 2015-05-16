#include "GEK\Utility\FileSystem.h"
#include "GEK\Utility\String.h"
#include "GEK\Context\Common.h"
#include "GEK\Context\ContextInterface.h"
#include "GEK\Context\ContextUserInterface.h"
#include "GEK\Context\Observable.h"
#include <atlbase.h>
#include <atlstr.h>
#include <atlpath.h>
#include <list>
#include <unordered_map>

namespace Gek
{
    class Context : public Observable
                  , public ContextInterface
    {
    private:
        ULONG referenceCount;

        std::list<CStringW> searchPathList;

        std::list<HMODULE> moduleList;
        std::unordered_map<CLSID, std::function<HRESULT(ContextUserInterface **)>> classList;
        std::unordered_map<CLSID, std::vector<CLSID>> typedClassList;

        Handle nextListenerHandle;
        std::unordered_map<Handle, std::function<void(LPCSTR, UINT32, LPCWSTR)>> logMessageListenerList;
        UINT32 loggingIndent;

    public:
        Context(void)
            : referenceCount(0)
            , nextListenerHandle(InvalidHandle)
            , loggingIndent(0)
        {
        }

        ~Context(void)
        {
            classList.clear();
            typedClassList.clear();
            for (auto &module : moduleList)
            {
                FreeLibrary(module);
            }
        }

        // IUnknown
        STDMETHODIMP_(ULONG) AddRef(void)
        {
            return InterlockedIncrement(&referenceCount);
        }

        STDMETHODIMP_(ULONG) Release(void)
        {
            LONG newReferenceCount = InterlockedDecrement(&referenceCount);
            if (newReferenceCount == 0)
            {
                delete this;
            }

            return newReferenceCount;
        }

        STDMETHODIMP QueryInterface(REFIID interfaceType, LPVOID FAR *returnObject)
        {
            REQUIRE_RETURN(returnObject, E_INVALIDARG);

            HRESULT resultValue = E_INVALIDARG;
            if (IsEqualIID(IID_IUnknown, interfaceType))
            {
                AddRef();
                (*returnObject) = dynamic_cast<IUnknown *>(dynamic_cast<ContextInterface *>(this));
                _ASSERTE(*returnObject);
                resultValue = S_OK;
            }
            else if (IsEqualIID(__uuidof(ContextInterface), interfaceType))
            {
                AddRef();
                (*returnObject) = dynamic_cast<ContextInterface *>(this);
                _ASSERTE(*returnObject);
                resultValue = S_OK;
            }

            return resultValue;
        }

        // ContextInterface
        STDMETHODIMP_(void) addSearchPath(LPCWSTR fileName)
        {
            searchPathList.push_back(fileName);
        }

        STDMETHODIMP_(void) initialize(void)
        {
            logMessage(__FILE__, __LINE__, L"> Entering %S...", __FUNCTION__);
            logEnterScope();

            searchPathList.push_back(L"%root%");
            for (auto &searchPath : searchPathList)
            {
                Gek::FileSystem::find(searchPath, L"*.dll", false, [&](LPCWSTR fileName) -> HRESULT
                {
                    HMODULE module = LoadLibrary(fileName);
                    if (module)
                    {
                        typedef HRESULT(*GEKGETMODULECLASSES)(std::unordered_map<CLSID, std::function<HRESULT(ContextUserInterface **)>> &, std::unordered_map<CLSID, std::vector<CLSID >> &);
                        GEKGETMODULECLASSES getModuleClasses = (GEKGETMODULECLASSES)GetProcAddress(module, "GEKGetModuleClasses");
                        if (getModuleClasses)
                        {
                            logMessage(__FILE__, __LINE__, L"GEK Plugin Found: %s", fileName);

                            moduleList.push_back(module);
                            std::unordered_map<CLSID, std::function<HRESULT(ContextUserInterface **)>> moduleClassList;
                            std::unordered_map<CLSID, std::vector<CLSID>> moduleTypedClassList;

                            if (SUCCEEDED(getModuleClasses(moduleClassList, moduleTypedClassList)))
                            {
                                for (auto &moduleClass : moduleClassList)
                                {
                                    if (classList.find(moduleClass.first) == classList.end())
                                    {
                                        classList[moduleClass.first] = moduleClass.second;
                                        logMessage(__FILE__, __LINE__, L"Adding class from plugin: %s", CStringW(CComBSTR(moduleClass.first)).GetString());
                                    }
                                    else
                                    {
                                        logMessage(__FILE__, __LINE__, L"ERROR: Duplicate class found: %s", CStringW(CComBSTR(moduleClass.first)).GetString());
                                    }
                                }

                                for (auto &moduleTypedClass : moduleTypedClassList)
                                {
                                    typedClassList[moduleTypedClass.first].insert(typedClassList[moduleTypedClass.first].end(), moduleTypedClass.second.begin(), moduleTypedClass.second.end());
                                }
                            }
                            else
                            {
                                logMessage(__FILE__, __LINE__, L"ERROR: Unable to get class list from module");
                            }
                        }
                    }

                    return S_OK;
                });
            }

            logExitScope();
            logMessage(__FILE__, __LINE__, L"< Leaving %S", __FUNCTION__);
        }

        STDMETHODIMP createInstance(REFGUID classType, REFIID interfaceType, LPVOID FAR *returnObject)
        {
            REQUIRE_RETURN(returnObject, E_INVALIDARG);

            HRESULT resultValue = E_FAIL;
            auto classIterator = classList.find(classType);
            if (classIterator != classList.end())
            {
                CComPtr<ContextUserInterface> classInstance;
                resultValue = ((*classIterator).second)(&classInstance);
                if (SUCCEEDED(resultValue) && classInstance)
                {
                    classInstance->registerContext(this);
                    resultValue = classInstance->QueryInterface(interfaceType, returnObject);
                }
            }

            return resultValue;
        }

        STDMETHODIMP createEachType(REFCLSID typeID, std::function<HRESULT(REFCLSID, IUnknown *)> onCreateInstance)
        {
            HRESULT resultValue = S_OK;
            auto typedClassIterator = typedClassList.find(typeID);
            if (typedClassIterator != typedClassList.end())
            {
                for (auto &classType : (*typedClassIterator).second)
                {
                    CComPtr<IUnknown> classInstance;
                    resultValue = createInstance(classType, IID_PPV_ARGS(&classInstance));
                    if (classInstance)
                    {
                        resultValue = onCreateInstance(classType, classInstance);
                        if (FAILED(resultValue))
                        {
                            break;
                        }
                    }
                };
            }

            return resultValue;
        }

        STDMETHODIMP_(Handle) addLogListener(std::function<void(LPCSTR, UINT32, LPCWSTR)> onLogMessage)
        {
            Handle listenerHandle = InterlockedIncrement(&nextListenerHandle);
            logMessageListenerList[listenerHandle] = onLogMessage;
            return listenerHandle;
        }

        STDMETHODIMP_(void) removeLogListener(Handle listenerHandle)
        {
            auto listenerIterator = logMessageListenerList.find(listenerHandle);
            if (listenerIterator != logMessageListenerList.end())
            {
                logMessageListenerList.erase(listenerIterator);
            }
        }

        STDMETHODIMP_(void) logMessage(LPCSTR file, UINT32 line, LPCWSTR format, ...)
        {
            if (format != nullptr)
            {
                CStringW message;

                va_list variableList;
                va_start(variableList, format);
                message.FormatV(format, variableList);
                va_end(variableList);

                std::vector<wchar_t> indent(loggingIndent, L' ');
                indent.push_back(L'\0');

                message = (indent.data() + (L"- " + message));
                OutputDebugString(Gek::String::format(L"% 30S (%05d): %s\r\n", file, line, message.GetString()));
                for (auto &logMessageListener : logMessageListenerList)
                {
                    logMessageListener.second(file, line, message);
                }
            }
        }

        STDMETHODIMP_(void) logEnterScope(void)
        {
            InterlockedIncrement(&loggingIndent);
        }

        STDMETHODIMP_(void) logExitScope(void)
        {
            InterlockedDecrement(&loggingIndent);
        }
    };

    HRESULT createContext(ContextInterface **returnObject)
    {
        REQUIRE_RETURN(returnObject, E_INVALIDARG);

        HRESULT resultValue = E_OUTOFMEMORY;
        CComPtr<Context> context(new Context());
        _ASSERTE(context);
        if (context)
        {
            resultValue = context->QueryInterface(IID_PPV_ARGS(returnObject));
        }

        return resultValue;
    }
};