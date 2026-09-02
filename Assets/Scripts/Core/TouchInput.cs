using UnityEngine;
using UnityEngine.EventSystems;
#if ENABLE_INPUT_SYSTEM && !ENABLE_LEGACY_INPUT_MANAGER
using UnityEngine.InputSystem;
#endif

namespace MobClash.Core
{
    /// <summary>
    /// Backend agnostic single finger pointer reader.
    /// Compiles against the legacy Input Manager, the new Input System, or both
    /// (Project Settings -> Player -> Active Input Handling).
    /// The state is snapshotted once per frame so every consumer sees identical values.
    /// </summary>
    public static class TouchInput
    {
        private static int _lastFrame = -1;
        private static bool _isPressed;
        private static bool _wasPressed;
        private static Vector2 _position;
        private static Vector2 _previousPosition;
        private static Vector2 _delta;

        /// <summary>True while a finger (or the mouse button) is held down.</summary>
        public static bool IsPressed
        {
            get { EnsureFrame(); return _isPressed; }
        }

        /// <summary>True on the frame the press begins.</summary>
        public static bool PressedThisFrame
        {
            get { EnsureFrame(); return _isPressed && !_wasPressed; }
        }

        /// <summary>True on the frame the press ends.</summary>
        public static bool ReleasedThisFrame
        {
            get { EnsureFrame(); return !_isPressed && _wasPressed; }
        }

        /// <summary>Screen space pointer position in pixels.</summary>
        public static Vector2 Position
        {
            get { EnsureFrame(); return _position; }
        }

        /// <summary>Screen space movement since the previous frame, in pixels. Zero when not pressed.</summary>
        public static Vector2 DeltaPixels
        {
            get { EnsureFrame(); return _delta; }
        }

        /// <summary>True when the pointer is currently over an interactive UI element.</summary>
        public static bool IsPointerOverUI
        {
            get
            {
                EventSystem eventSystem = EventSystem.current;
                if (eventSystem == null) return false;

#if ENABLE_LEGACY_INPUT_MANAGER
                if (Input.touchCount > 0)
                {
                    return eventSystem.IsPointerOverGameObject(Input.GetTouch(0).fingerId);
                }
#endif
                return eventSystem.IsPointerOverGameObject();
            }
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void ResetStatics()
        {
            _lastFrame = -1;
            _isPressed = false;
            _wasPressed = false;
            _position = Vector2.zero;
            _previousPosition = Vector2.zero;
            _delta = Vector2.zero;
        }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void InstallPump()
        {
            GameObject go = new GameObject("~TouchInputPump");
            go.hideFlags = HideFlags.HideAndDontSave;
            go.AddComponent<TouchInputPump>();
            Object.DontDestroyOnLoad(go);
        }

        private static void EnsureFrame()
        {
            int frame = Time.frameCount;
            if (frame == _lastFrame) return;
            _lastFrame = frame;

            _wasPressed = _isPressed;
            _previousPosition = _position;

            bool pressed = false;
            Vector2 position = _position;
            ReadRawPointer(ref pressed, ref position);

            _isPressed = pressed;
            _position = position;

            if (_isPressed && _wasPressed)
            {
                _delta = _position - _previousPosition;
            }
            else
            {
                _delta = Vector2.zero;
            }
        }

        private static void ReadRawPointer(ref bool pressed, ref Vector2 position)
        {
#if ENABLE_LEGACY_INPUT_MANAGER
            if (Input.touchCount > 0)
            {
                Touch touch = Input.GetTouch(0);
                pressed = touch.phase != TouchPhase.Ended && touch.phase != TouchPhase.Canceled;
                position = touch.position;
                return;
            }

            if (Input.GetMouseButton(0) || Input.GetMouseButtonDown(0))
            {
                pressed = true;
                position = Input.mousePosition;
                return;
            }

            pressed = false;
            position = Input.mousePosition;
#elif ENABLE_INPUT_SYSTEM
            Touchscreen touchscreen = Touchscreen.current;
            if (touchscreen != null && touchscreen.primaryTouch.press.isPressed)
            {
                pressed = true;
                position = touchscreen.primaryTouch.position.ReadValue();
                return;
            }

            Mouse mouse = Mouse.current;
            if (mouse != null)
            {
                pressed = mouse.leftButton.isPressed;
                position = mouse.position.ReadValue();
                return;
            }

            pressed = false;
#else
            pressed = false;
#endif
        }

        /// <summary>Hidden driver that guarantees the snapshot advances even on frames nobody polls.</summary>
        private sealed class TouchInputPump : MonoBehaviour
        {
            private void LateUpdate()
            {
                EnsureFrame();
            }
        }
    }
}
