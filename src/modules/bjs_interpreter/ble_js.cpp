#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
#include "ble_js.h"
#include "modules/ble/ble_common.h"
#include "modules/ble/ble_spam.h"

JSValue native_bleScan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    // usage: ble.scan(timeout_seconds)
    // returns: array of objects [{name, address, rssi}, ...]
    int timeout = 5; // default 5 seconds
    if (argc > 0 && JS_IsNumber(ctx, argv[0])) {
        JS_ToInt32(ctx, &timeout, argv[0]);
    }
    if (timeout < 1) timeout = 1;
    if (timeout > 30) timeout = 30;

    BLEDevice::init("");
    NimBLEScan *pScan = BLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);

#if __has_include(<NimBLEExtAdvertising.h>)
    BLEScanResults results = pScan->getResults(timeout * 1000, false);
    int count = results.getCount();
#else
    BLEScanResults results = pScan->start(timeout, false);
    int count = results.getCount();
#endif

    // Pinned: the loop below allocates per device, which can compact the heap.
    // See native_wifiGetCapturedPackets() in wifi_js.cpp for the full hazard.
    JSGCRef arr_ref;
    JSValue *arr = JS_PushGCRef(ctx, &arr_ref);
    *arr = JS_NewArray(ctx, 0);
    if (JS_IsException(*arr)) {
        JS_PopGCRef(ctx, &arr_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    for (int i = 0; i < count; i++) {
#if __has_include(<NimBLEExtAdvertising.h>)
        const NimBLEAdvertisedDevice *dev = results.getDevice(i);
#else
        NimBLEAdvertisedDevice dev_obj = results.getDevice(i);
        NimBLEAdvertisedDevice *dev = &dev_obj;
#endif
        JSGCRef obj_ref;
        JSValue *obj = JS_PushGCRef(ctx, &obj_ref);
        *obj = JS_NewObject(ctx);
        if (JS_IsException(*obj)) {
            JS_PopGCRef(ctx, &obj_ref);
            JS_PopGCRef(ctx, &arr_ref);
            return JS_ThrowOutOfMemory(ctx);
        }
        String name = dev->getName().c_str();
        if (name.isEmpty()) name = "<unknown>";
        JS_SetPropertyStr(ctx, *obj, "name", JS_NewString(ctx, name.c_str()));
        JS_SetPropertyStr(ctx, *obj, "address", JS_NewString(ctx, dev->getAddress().toString().c_str()));
        JS_SetPropertyStr(ctx, *obj, "rssi", JS_NewInt32(ctx, dev->getRSSI()));
        JS_SetPropertyUint32(ctx, *arr, i, *obj);
        JS_PopGCRef(ctx, &obj_ref);
    }

    pScan->clearResults();
    return JS_PopGCRef(ctx, &arr_ref);
}

JSValue native_bleAdvertise(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    // usage: ble.advertise(name_string)
    const char *name = "Bruce-App";
    JSCStringBuf name_buf;
    if (argc > 0 && JS_IsString(ctx, argv[0])) {
        name = JS_ToCString(ctx, argv[0], &name_buf);
    }

    BLEDevice::init(name);
    NimBLEServer *pServer = BLEDevice::createServer();
    pServer->getAdvertising()->start();

    return JS_NewBool(true);
}

JSValue native_bleStopAdvertise(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    NimBLEDevice::getAdvertising()->stop();
    BLEDevice::deinit();
    return JS_UNDEFINED;
}

// ============================================================================
// BLE spam bindings.
// The on-screen spam menu blocks until a button is pressed, so it cannot be
// called from a script. These bindings drive the same advertisement builders
// but stop on a deadline.
// ============================================================================

JSValue native_bleSpamModes(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    // usage: ble.spamModes()
    // returns: array of selectable spam mode names, indexed as ble.spam() expects
    int count = headlessBleSpamModeCount();
    JSGCRef arr_ref;
    JSValue *arr = JS_PushGCRef(ctx, &arr_ref);
    *arr = JS_NewArray(ctx, count);
    if (JS_IsException(*arr)) {
        JS_PopGCRef(ctx, &arr_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    for (int i = 0; i < count; i++) {
        JS_SetPropertyUint32(ctx, *arr, i, JS_NewString(ctx, headlessBleSpamModeName(i)));
    }
    return JS_PopGCRef(ctx, &arr_ref);
}

JSValue native_bleSpam(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) {
    // usage: ble.spam(type?: int | string, seconds?: int)
    //   type: index into ble.spamModes() (default 0), or a mode name such as
    //         "Samsung BLE Spam" / "Random / All"
    // returns: number of advertisement packets sent
    int type = 0;
    if (argc > 0 && JS_IsNumber(ctx, argv[0])) {
        JS_ToInt32(ctx, &type, argv[0]);
    } else if (argc > 0 && JS_IsString(ctx, argv[0])) {
        JSCStringBuf nb;
        const char *name = JS_ToCString(ctx, argv[0], &nb);
        type = -1;
        if (name != NULL) {
            int count = headlessBleSpamModeCount();
            for (int i = 0; i < count; i++) {
                if (strcasecmp(name, headlessBleSpamModeName(i)) == 0) {
                    type = i;
                    break;
                }
            }
        }
        if (type < 0) type = 0;
    }

    int seconds = 10;
    if (argc > 1 && JS_IsNumber(ctx, argv[1])) JS_ToInt32(ctx, &seconds, argv[1]);
    if (seconds < 1) seconds = 1;
    if (seconds > 300) seconds = 300;

    int sent = headlessBleSpam((uint8_t)type, (uint32_t)seconds * 1000);
    return JS_NewInt32(ctx, sent);
}

#endif
