using System;
using System.IO;
using System.Reflection;
using IPA;
using IPA.Logging;
using Newtonsoft.Json.Linq;
using SiraUtil.Zenject;
using UnityEngine;
using Zenject;

namespace BARBAROSSOMOD
{
    [Plugin(RuntimeOptions.DynamicInit)]
    public sealed class Plugin
    {
        internal static IPA.Logging.Logger Log;

        [Init]
        public Plugin(IPA.Logging.Logger logger, Zenjector zenjector)
        {
            Log = logger;
            zenjector.Install<MapImageInstaller>(Location.Player);
            Log.Info("BARBAROSSOMOD v0.1 initialized for map-owned backgrounds.");
        }

        [OnEnable]
        public void OnEnable() { }

        [OnDisable]
        public void OnDisable() { }
    }

    internal sealed class MapImageInstaller : Installer
    {
        public override void InstallBindings()
        {
            Container.BindInterfacesAndSelfTo<MapImageController>().AsSingle().NonLazy();
        }
    }

    internal sealed class MapImageController : IDisposable
    {
        private GameObject _quad;
        private Texture2D _texture;
        private AssetBundle _assetBundle;
        private Material _imageMaterial;

        public MapImageController(BeatmapLevel beatmapLevel)
        {
            try
            {
                LoadForLevel(beatmapLevel);
            }
            catch (Exception exception)
            {
                Plugin.Log.Error("Map image failed: " + exception);
            }
        }

        private void LoadForLevel(BeatmapLevel beatmapLevel)
        {
            if (!ReadGlobalEnabled())
            {
                Plugin.Log.Info("BARBAROSSOMOD is disabled in UserData/BARBAROSSOMOD.json.");
                return;
            }

            string levelFolder = FindLevelFolder(beatmapLevel);
            if (String.IsNullOrEmpty(levelFolder))
            {
                Plugin.Log.Debug("Selected level is not a file-system custom level.");
                return;
            }

            string infoPath = Path.Combine(levelFolder, "Info.dat");
            if (!File.Exists(infoPath)) infoPath = Path.Combine(levelFolder, "info.dat");
            if (!File.Exists(infoPath)) return;

            JObject info = JObject.Parse(File.ReadAllText(infoPath));
            JObject mapImage = info["_customData"] == null ? null : info["_customData"]["_mapImage"] as JObject;
            if (mapImage == null || !Value(mapImage, "_enabled", true))
            {
                Plugin.Log.Debug("No enabled _customData._mapImage for " + beatmapLevel.songName);
                return;
            }

            string fileName = Value(mapImage, "_file", "");
            if (!IsSafeFileName(fileName))
            {
                Plugin.Log.Warn("Rejected unsafe map image name: " + fileName);
                return;
            }
            string extension = Path.GetExtension(fileName).ToLowerInvariant();
            if (extension != ".png" && extension != ".jpg" && extension != ".jpeg")
            {
                Plugin.Log.Warn("Map image must be PNG or JPEG: " + fileName);
                return;
            }

            string imagePath = Path.Combine(levelFolder, fileName);
            if (!File.Exists(imagePath))
            {
                Plugin.Log.Warn("Map image file not found: " + imagePath);
                return;
            }

            Vector3 position = Vector(mapImage, "_position", new Vector3(0f, 11f, 45f));
            Vector3 rotation = Vector(mapImage, "_rotation", Vector3.zero);
            Vector3 configuredScale = Vector(mapImage, "_scale", new Vector3(3200f, 2133f, 1f));

            byte[] bytes = File.ReadAllBytes(imagePath);
            _texture = new Texture2D(2, 2, TextureFormat.RGBA32, false);
            if (!ImageConversion.LoadImage(_texture, bytes, true))
            {
                throw new InvalidDataException("Unity could not decode " + imagePath);
            }
            _texture.name = "BARBAROSSO_MapImage_Texture";
            _texture.wrapMode = TextureWrapMode.Clamp;
            _texture.filterMode = FilterMode.Bilinear;

            string pluginFolder = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            string bundlePath = Path.Combine(pluginFolder, "MapImagePCAssets.bundle");
            if (!File.Exists(bundlePath))
            {
                throw new FileNotFoundException("BARBAROSSOMOD no-bloom shader bundle is missing.", bundlePath);
            }
            _assetBundle = AssetBundle.LoadFromFile(bundlePath);
            if (_assetBundle == null) throw new InvalidDataException("Unity could not load " + bundlePath);
            Shader noBloomShader = null;
            foreach (Shader shader in _assetBundle.LoadAllAssets<Shader>())
            {
                if (shader != null && shader.name == "BARBAROSSO/MapImageNoBloom")
                {
                    noBloomShader = shader;
                    break;
                }
            }
            if (noBloomShader == null) throw new InvalidDataException("No MapImageNoBloom shader in " + bundlePath);
            _imageMaterial = new Material(noBloomShader);
            _imageMaterial.name = "BARBAROSSO_MapImage_NoBloom_Material";
            _imageMaterial.SetTexture("_MainTex", _texture);
            _imageMaterial.enableInstancing = true;

            // The Quest-compatible layout is retained, but PC BSML Canvas objects
            // disappear from the direct stereo eye pass when fragment alpha is
            // zero. The original 1.0.0 Quad was visible directly in both eyes;
            // combine that proven carrier with the V9-proven stereo shader and
            // literal output alpha zero.
            _quad = GameObject.CreatePrimitive(PrimitiveType.Quad);
            _quad.name = "BARBAROSSO_MAP_OWNED_BACKGROUND";
            // Verified V9 rule: map-owned stage geometry belongs to Environment
            // layer 14. The road MirrorCam excludes Default layer 0.
            _quad.layer = 14;
            Collider collider = _quad.GetComponent<Collider>();
            if (collider != null) UnityEngine.Object.Destroy(collider);
            MeshRenderer renderer = _quad.GetComponent<MeshRenderer>();
            renderer.sharedMaterial = _imageMaterial;
            renderer.receiveShadows = false;
            renderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            _quad.transform.position = position;
            _quad.transform.rotation = Quaternion.Euler(rotation);
            _quad.transform.localScale = new Vector3(
                configuredScale.x * 0.02f,
                configuredScale.y * 0.02f,
                1f);

            Plugin.Log.Info(String.Format(
                "Loaded map-owned instanced stereo Quad image for '{0}': {1}, texture={2}x{3}, shader={4}, position={5}, worldSize={6}x{7}, layer=14, instancing={8}, outputAlpha=0",
                beatmapLevel.songName, imagePath,
                _texture.width, _texture.height, noBloomShader.name, position,
                _quad.transform.localScale.x, _quad.transform.localScale.y,
                _imageMaterial.enableInstancing));
        }

        private static bool ReadGlobalEnabled()
        {
            string path = Path.Combine(Environment.CurrentDirectory, "UserData", "BARBAROSSOMOD.json");
            if (!File.Exists(path))
            {
                // Compatibility with private development builds used before v0.1.
                path = Path.Combine(Environment.CurrentDirectory, "UserData", "MapImage.json");
            }
            if (!File.Exists(path)) return true;
            try
            {
                JObject config = JObject.Parse(File.ReadAllText(path));
                return Value(config, "Enabled", true);
            }
            catch (Exception exception)
            {
                Plugin.Log.Warn("Could not parse BARBAROSSOMOD configuration; using Enabled=true: " + exception.Message);
                return true;
            }
        }

        private static string FindLevelFolder(BeatmapLevel level)
        {
            object preview = level.previewMediaData;
            if (preview == null) return null;
            Type type = preview.GetType();
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
            foreach (string name in new[] { "_previewAudioClipPath", "previewAudioClipPath" })
            {
                FieldInfo field = type.GetField(name, flags);
                if (field != null)
                {
                    string path = field.GetValue(preview) as string;
                    if (!String.IsNullOrEmpty(path)) return Path.GetDirectoryName(path);
                }
                PropertyInfo property = type.GetProperty(name, flags);
                if (property != null)
                {
                    string path = property.GetValue(preview, null) as string;
                    if (!String.IsNullOrEmpty(path)) return Path.GetDirectoryName(path);
                }
            }
            return null;
        }

        private static bool IsSafeFileName(string value)
        {
            return !String.IsNullOrWhiteSpace(value) &&
                   value == Path.GetFileName(value) &&
                   value.IndexOf("..", StringComparison.Ordinal) < 0 &&
                   value.IndexOf('/') < 0 && value.IndexOf('\\') < 0;
        }

        private static T Value<T>(JObject obj, string name, T fallback)
        {
            JToken token = obj[name];
            if (token == null || token.Type == JTokenType.Null) return fallback;
            try { return token.Value<T>(); }
            catch { return fallback; }
        }

        private static Vector3 Vector(JObject obj, string name, Vector3 fallback)
        {
            JArray array = obj[name] as JArray;
            if (array == null || array.Count < 2) return fallback;
            float z = array.Count > 2 ? array[2].Value<float>() : fallback.z;
            return new Vector3(array[0].Value<float>(), array[1].Value<float>(), z);
        }

        public void Dispose()
        {
            if (_quad != null) UnityEngine.Object.Destroy(_quad);
            if (_imageMaterial != null) UnityEngine.Object.Destroy(_imageMaterial);
            if (_texture != null) UnityEngine.Object.Destroy(_texture);
            if (_assetBundle != null) _assetBundle.Unload(false);
            _quad = null;
            _imageMaterial = null;
            _texture = null;
            _assetBundle = null;
        }
    }
}
