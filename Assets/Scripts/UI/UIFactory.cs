using MobClash.Core;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace MobClash.UI
{
    /// <summary>
    /// Code driven uGUI construction helpers. Building the HUD from code keeps the repository
    /// prefab free and makes the whole interface diffable and reviewable.
    /// Swap these calls for real prefabs by assigning the reference fields on <see cref="UIManager"/>.
    /// </summary>
    public static class UIFactory
    {
        public static readonly Vector2 ReferenceResolution = new Vector2(1080f, 1920f);

        public static readonly Color Ink = new Color(0.99f, 0.99f, 1f, 1f);
        public static readonly Color Accent = new Color(0.20f, 0.72f, 1f, 1f);
        public static readonly Color Positive = new Color(0.18f, 0.80f, 0.42f, 1f);
        public static readonly Color Warning = new Color(1f, 0.72f, 0.16f, 1f);
        public static readonly Color Danger = new Color(0.92f, 0.28f, 0.30f, 1f);
        public static readonly Color PanelBackground = new Color(0.05f, 0.06f, 0.11f, 0.88f);

        /// <summary>Creates a screen space canvas that scales with the device resolution.</summary>
        public static Canvas CreateCanvas(string name, int sortOrder, Transform parent)
        {
            GameObject go = new GameObject(name);
            if (parent != null) go.transform.SetParent(parent, false);

            Canvas canvas = go.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.sortingOrder = sortOrder;

            CanvasScaler scaler = go.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = ReferenceResolution;
            scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.MatchWidthOrHeight;
            scaler.matchWidthOrHeight = 0.5f;

            go.AddComponent<GraphicRaycaster>();
            return canvas;
        }

        /// <summary>Guarantees an EventSystem exists using whichever input backend is active.</summary>
        public static void EnsureEventSystem()
        {
            if (EventSystem.current != null) return;

#if UNITY_2023_1_OR_NEWER
            EventSystem existing = Object.FindFirstObjectByType<EventSystem>();
#else
            EventSystem existing = Object.FindObjectOfType<EventSystem>();
#endif
            if (existing != null) return;

            GameObject go = new GameObject("EventSystem");
            go.AddComponent<EventSystem>();

#if ENABLE_INPUT_SYSTEM && !ENABLE_LEGACY_INPUT_MANAGER
            go.AddComponent<UnityEngine.InputSystem.UI.InputSystemUIInputModule>();
#else
            go.AddComponent<StandaloneInputModule>();
#endif
        }

        public static RectTransform Stretch(RectTransform rect, float left, float bottom, float right, float top)
        {
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = new Vector2(left, bottom);
            rect.offsetMax = new Vector2(-right, -top);
            return rect;
        }

        public static RectTransform Anchor(RectTransform rect, Vector2 anchorMin, Vector2 anchorMax,
            Vector2 anchoredPosition, Vector2 sizeDelta)
        {
            rect.anchorMin = anchorMin;
            rect.anchorMax = anchorMax;
            rect.pivot = new Vector2(0.5f, 0.5f);
            rect.anchoredPosition = anchoredPosition;
            rect.sizeDelta = sizeDelta;
            return rect;
        }

        public static GameObject Panel(Transform parent, string name, Color color)
        {
            GameObject go = new GameObject(name);
            go.transform.SetParent(parent, false);

            Image image = go.AddComponent<Image>();
            image.color = color;
            Stretch(image.rectTransform, 0f, 0f, 0f, 0f);

            return go;
        }

        public static Image Box(Transform parent, string name, Color color,
            Vector2 anchorMin, Vector2 anchorMax, Vector2 position, Vector2 size)
        {
            GameObject go = new GameObject(name);
            go.transform.SetParent(parent, false);

            Image image = go.AddComponent<Image>();
            image.color = color;
            Anchor(image.rectTransform, anchorMin, anchorMax, position, size);

            return image;
        }

        public static Text Label(Transform parent, string name, string content, int fontSize,
            TextAnchor alignment, Color color,
            Vector2 anchorMin, Vector2 anchorMax, Vector2 position, Vector2 size)
        {
            GameObject go = new GameObject(name);
            go.transform.SetParent(parent, false);

            Text text = go.AddComponent<Text>();
            text.font = PrimitiveFactory.BuiltinFont();
            text.text = content;
            text.fontSize = fontSize;
            text.fontStyle = FontStyle.Bold;
            text.alignment = alignment;
            text.color = color;
            text.horizontalOverflow = HorizontalWrapMode.Overflow;
            text.verticalOverflow = VerticalWrapMode.Overflow;
            text.raycastTarget = false;

            Anchor(text.rectTransform, anchorMin, anchorMax, position, size);
            return text;
        }

        public static Button TextButton(Transform parent, string name, string caption, Color background,
            Vector2 anchorMin, Vector2 anchorMax, Vector2 position, Vector2 size, int fontSize)
        {
            GameObject go = new GameObject(name);
            go.transform.SetParent(parent, false);

            Image image = go.AddComponent<Image>();
            image.color = background;
            Anchor(image.rectTransform, anchorMin, anchorMax, position, size);

            Button button = go.AddComponent<Button>();
            button.targetGraphic = image;

            ColorBlock colors = button.colors;
            colors.normalColor = Color.white;
            colors.highlightedColor = new Color(1.05f, 1.05f, 1.05f, 1f);
            colors.pressedColor = new Color(0.82f, 0.82f, 0.82f, 1f);
            colors.disabledColor = new Color(0.55f, 0.55f, 0.55f, 0.7f);
            colors.fadeDuration = 0.06f;
            button.colors = colors;

            Text label = Label(go.transform, "Label", caption, fontSize, TextAnchor.MiddleCenter, Ink,
                Vector2.zero, Vector2.one, Vector2.zero, Vector2.zero);
            Stretch(label.rectTransform, 8f, 4f, 8f, 4f);

            return button;
        }

        /// <summary>Full screen invisible button, used for "tap anywhere to start".</summary>
        public static Button FullScreenButton(Transform parent, string name)
        {
            GameObject go = new GameObject(name);
            go.transform.SetParent(parent, false);

            Image image = go.AddComponent<Image>();
            image.color = new Color(0f, 0f, 0f, 0f);
            Stretch(image.rectTransform, 0f, 0f, 0f, 0f);

            Button button = go.AddComponent<Button>();
            button.targetGraphic = image;
            button.transition = Selectable.Transition.None;
            return button;
        }
    }
}
