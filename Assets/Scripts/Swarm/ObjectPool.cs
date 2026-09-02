using System.Collections.Generic;
using UnityEngine;

namespace MobClash.Swarm
{
    /// <summary>
    /// Minimal, allocation free GameObject pool. Sized for 500+ simultaneous crowd members on
    /// mid range Android hardware: no Instantiate/Destroy churn during a run, no GC spikes.
    /// </summary>
    public class ObjectPool : MonoBehaviour
    {
        [Tooltip("Prefab to pool. When empty the owner supplies one through Initialise().")]
        public GameObject prefab;

        [Tooltip("Instances created up front, before the first level starts.")]
        public int prewarmCount = 320;

        [Tooltip("Hard ceiling on live instances. Requests beyond this return null.")]
        public int hardCap = 1200;

        [Tooltip("Parent for inactive instances. Created automatically when left empty.")]
        public Transform poolParent;

        private readonly Stack<GameObject> _available = new Stack<GameObject>(512);
        private int _liveCount;
        private bool _initialised;

        public int LiveCount { get { return _liveCount; } }
        public int AvailableCount { get { return _available.Count; } }

        private void Awake()
        {
            if (prefab != null) Initialise(prefab, prewarmCount);
        }

        /// <summary>Assigns the prefab and prewarms the pool. Safe to call twice with the same prefab.</summary>
        public void Initialise(GameObject sourcePrefab, int prewarm)
        {
            if (sourcePrefab == null) return;

            if (_initialised && prefab == sourcePrefab)
            {
                Prewarm(prewarm);
                return;
            }

            if (_initialised && prefab != sourcePrefab) Clear();

            prefab = sourcePrefab;
            _initialised = true;

            if (poolParent == null)
            {
                GameObject holder = new GameObject("PoolParent_" + sourcePrefab.name);
                holder.transform.SetParent(transform, false);
                poolParent = holder.transform;
            }

            Prewarm(prewarm);
        }

        public void Prewarm(int count)
        {
            count = Mathf.Min(count, hardCap);
            while (_available.Count + _liveCount < count)
            {
                GameObject instance = CreateInstance();
                if (instance == null) return;
                instance.SetActive(false);
                _available.Push(instance);
            }
        }

        public GameObject Get()
        {
            GameObject instance;
            if (_available.Count > 0)
            {
                instance = _available.Pop();
            }
            else
            {
                if (_liveCount >= hardCap) return null;
                instance = CreateInstance();
                if (instance == null) return null;
            }

            instance.SetActive(true);
            _liveCount++;
            return instance;
        }

        public void Release(GameObject instance)
        {
            if (instance == null) return;

            instance.SetActive(false);
            if (poolParent != null) instance.transform.SetParent(poolParent, false);

            _available.Push(instance);
            _liveCount = Mathf.Max(0, _liveCount - 1);
        }

        public void Clear()
        {
            while (_available.Count > 0)
            {
                GameObject instance = _available.Pop();
                if (instance != null) Destroy(instance);
            }

            _liveCount = 0;
            _initialised = false;
        }

        private GameObject CreateInstance()
        {
            if (prefab == null) return null;

            GameObject instance = Instantiate(prefab, poolParent != null ? poolParent : transform);
            instance.name = prefab.name;
            return instance;
        }
    }
}
