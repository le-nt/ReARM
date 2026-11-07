package com.roblox.client;

import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.SurfaceView;
import android.view.View;

/**
 * Translates Android touch events into the input ABI used by the bundled legacy engine.
 * The engine expects pointer positions and surface dimensions in density-independent pixels.
 */
public final class InputListener implements View.OnTouchListener {
    private static final int TOUCH_BEGIN = 0;
    private static final int TOUCH_MOVE = 1;
    private static final int TOUCH_END = 2;

    private final SurfaceView surfaceView;
    private final float displayDensity;
    private final SparseArray<TouchInfo> activePointers = new SparseArray<>();

    public InputListener(ActivityGlView activity, SurfaceView surfaceView) {
        this.surfaceView = surfaceView;
        displayDensity = activity.getResources().getDisplayMetrics().density;
    }

    @Override
    public boolean onTouch(View view, MotionEvent event) {
        final int action = event.getActionMasked();
        final int actionIndex = event.getActionIndex();
        final int pointerId = event.getPointerId(actionIndex);

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                updatePointer(pointerId, event, actionIndex, TOUCH_BEGIN);
                break;
            case MotionEvent.ACTION_MOVE:
                // Track IDs instead of event indices: Android may reorder its pointer list.
                for (int index = 0; index < activePointers.size(); index++) {
                    int id = activePointers.keyAt(index);
                    int eventIndex = event.findPointerIndex(id);
                    if (eventIndex >= 0) {
                        updatePointer(id, event, eventIndex, TOUCH_MOVE);
                    }
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                updatePointer(pointerId, event, actionIndex, TOUCH_END);
                break;
            default:
                break;
        }

        dispatchChangedPointers();
        return true;
    }

    private void updatePointer(int pointerId, MotionEvent event, int index, int eventType) {
        TouchInfo info = activePointers.get(pointerId);
        if (info == null) {
            info = new TouchInfo();
            activePointers.put(pointerId, info);
        }
        info.setX(toDp(event.getX(index)));
        info.setY(toDp(event.getY(index)));
        info.setEventType(eventType);
    }

    private void dispatchChangedPointers() {
        final int width = toDp(surfaceView.getWidth());
        final int height = toDp(surfaceView.getHeight());
        // Iterate backwards: ended pointers are removed after their final event is sent.
        for (int index = activePointers.size() - 1; index >= 0; index--) {
            int pointerId = activePointers.keyAt(index);
            TouchInfo info = activePointers.valueAt(index);
            boolean ended = info.eventType == TOUCH_END;
            if (ended || info.hasChanged()) {
                if (ActivityGlView.getSurfaceState() == 2) {
                    nativePassInput(pointerId, info.x, info.y, info.eventType, width, height);
                    info.markDispatched();
                }
            }
            if (ended) {
                activePointers.removeAt(index);
            }
        }
    }

    private int toDp(float pixels) {
        return (int) (pixels / displayDensity);
    }

    private static final class TouchInfo {
        int x;
        int y;
        int eventType;
        int lastX;
        int lastY;
        int lastEventType;

        void setX(int value) {
            lastX = x;
            x = value;
        }

        void setY(int value) {
            lastY = y;
            y = value;
        }

        void setEventType(int value) {
            lastEventType = eventType;
            eventType = value;
        }

        boolean hasChanged() {
            return eventType != lastEventType || x != lastX || y != lastY;
        }

        void markDispatched() {
            lastX = x;
            lastY = y;
            lastEventType = eventType;
        }
    }

    // These declarations are part of libroblox.so's Java ABI. The first six are actively
    // used above; the rest retain compatibility with native code paths such as controllers.
    public static native void nativePassInput(int pointerId, int x, int y, int eventType,
                                               int width, int height);
    public static native void nativeGamepadConnectEvent(int deviceId);
    public static native void nativeGamepadDisconnectEvent(int deviceId);
    public static native void nativeSetGamepadSupportedKey(int deviceId, int keyCode, boolean supported);
    public static native void nativeGamepadButtonEvent(int deviceId, int keyCode, int action);
    public static native void nativeGamepadAxisEvent(int deviceId, int axis, float value, float min, float max);
    public static native void nativePassGravityChange(float x, float y, float z);
    public static native void nativePassAccelerometerChange(float x, float y, float z);
    public static native void nativePassGyroscopeChange(float x, float y, float z, float w, float a, float b, float c);
    public static native void nativeSetAccelerometerEnabled(boolean enabled);
    public static native void nativeSetGyroscopeEnabled(boolean enabled);
    public static native void nativePassSwipeGesture(int direction, int touches);
    public static native void nativePassPinchGesture(int state, float scale, float velocity, int x, int y, int x2, int y2);
    public static native void nativePassTapGesture(int state, int x, int y);
    public static native void nativePassLongPressGesture(int state, int x, int y);
    public static native void nativePassPanGesture(int state, int touches, int direction, float x, float y, float velocity);
    public static native void nativePassRotateGesture(int state, float rotation, float velocity, int x, int y, int x2, int y2);
}
