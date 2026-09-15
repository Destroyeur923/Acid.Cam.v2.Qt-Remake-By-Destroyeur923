#include "camera_enum.h"

#ifdef _WIN32
#include <windows.h>
#include <dshow.h>
#endif

namespace {

// Last-resort list: try to open a few indices and keep the ones that answer.
// Opening a camera is slow and briefly lights the capture LED, so the range
// is deliberately short - machines with more than 6 cameras are not the case
// this has to serve.
QVector<CameraDevice> probeCameras() {
    QVector<CameraDevice> found;
    for(int i = 0; i < 6; ++i) {
        cv::VideoCapture cap(i);
        if(cap.isOpened()) {
            found.push_back({i, QObject::tr("Caméra %1").arg(i)});
            cap.release();
        }
    }
    return found;
}

#ifdef _WIN32
// DirectShow enumerates the video input devices in the same order that
// OpenCV's DSHOW/MSMF backends index them, so the position in this list is
// the index to hand to cv::VideoCapture.
QVector<CameraDevice> enumerateCamerasDirectShow() {
    QVector<CameraDevice> found;

    // The app may or may not have initialised COM already; either is fine, we
    // only have to balance our own call.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool we_initialised = SUCCEEDED(init);

    ICreateDevEnum *dev_enum = nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dev_enum)))) {
        IEnumMoniker *enum_moniker = nullptr;
        // Returns S_FALSE (not an error) when the category exists but is
        // empty - i.e. no camera at all.
        if(dev_enum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enum_moniker, 0) == S_OK) {
            IMoniker *moniker = nullptr;
            int index = 0;
            while(enum_moniker->Next(1, &moniker, nullptr) == S_OK) {
                IPropertyBag *props = nullptr;
                QString name;
                if(SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&props)))) {
                    VARIANT var;
                    VariantInit(&var);
                    // FriendlyName is what Windows shows in its own device
                    // lists; Description exists only on some drivers.
                    if(SUCCEEDED(props->Read(L"FriendlyName", &var, nullptr)) && var.bstrVal)
                        name = QString::fromWCharArray(var.bstrVal);
                    VariantClear(&var);
                    props->Release();
                }
                if(name.isEmpty())
                    name = QObject::tr("Caméra %1").arg(index);
                found.push_back({index, name});
                ++index;
                moniker->Release();
            }
            enum_moniker->Release();
        }
        dev_enum->Release();
    }

    if(we_initialised)
        CoUninitialize();
    return found;
}
#endif

}  // namespace

QVector<CameraDevice> enumerateCameras() {
#ifdef _WIN32
    QVector<CameraDevice> found = enumerateCamerasDirectShow();
    if(!found.isEmpty())
        return found;
#endif
    return probeCameras();
}
