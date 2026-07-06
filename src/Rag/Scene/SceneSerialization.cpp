#include "Rag/Scene/SceneSerialization.h"

#include "Rag/Core/Log.h"

// nlohmann/json is vendored as a single header; keep it confined to this TU so it
// never lands in anyone else's include path (RagScene stays otherwise header-light).
#include <json.hpp>

#include <exception>
#include <fstream>
#include <sstream>
#include <string_view>

namespace rag::scene
{
    namespace
    {
        using nlohmann::json;

        // --- value-type <-> JSON array helpers ---------------------------------

        [[nodiscard]] json ToJson(const math::Vec3& v)
        {
            return json::array({v.x, v.y, v.z});
        }

        [[nodiscard]] json ToJson(const math::Quat& q)
        {
            return json::array({q.x, q.y, q.z, q.w});
        }

        [[nodiscard]] math::Vec3 Vec3FromJson(const json& node, const math::Vec3& fallback)
        {
            if (!node.is_array() || node.size() != 3)
            {
                return fallback;
            }
            return math::Vec3{
                node[0].get<f32>(),
                node[1].get<f32>(),
                node[2].get<f32>()};
        }

        [[nodiscard]] math::Quat QuatFromJson(const json& node, const math::Quat& fallback)
        {
            if (!node.is_array() || node.size() != 4)
            {
                return fallback;
            }
            return math::Quat{
                node[0].get<f32>(),
                node[1].get<f32>(),
                node[2].get<f32>(),
                node[3].get<f32>()};
        }

        // --- enum <-> string --------------------------------------------------

        [[nodiscard]] const char* ProjectionToString(CameraProjectionMode mode)
        {
            return mode == CameraProjectionMode::Orthographic ? "Orthographic" : "Perspective";
        }

        [[nodiscard]] CameraProjectionMode ProjectionFromString(std::string_view text)
        {
            return text == "Orthographic"
                ? CameraProjectionMode::Orthographic
                : CameraProjectionMode::Perspective;
        }

        [[nodiscard]] const char* LightTypeToString(renderer::RenderLightType type)
        {
            switch (type)
            {
                case renderer::RenderLightType::Directional: return "Directional";
                case renderer::RenderLightType::Point: return "Point";
                case renderer::RenderLightType::Spot: return "Spot";
            }
            return "Directional";
        }

        [[nodiscard]] renderer::RenderLightType LightTypeFromString(std::string_view text)
        {
            if (text == "Point")
            {
                return renderer::RenderLightType::Point;
            }
            if (text == "Spot")
            {
                return renderer::RenderLightType::Spot;
            }
            return renderer::RenderLightType::Directional;
        }

        // --- component writers -------------------------------------------------

        [[nodiscard]] json WriteTransform(const SerializedTransform& t)
        {
            json node;
            node["position"] = ToJson(t.position);
            node["rotationEulerRadians"] = ToJson(t.rotation_euler_radians);
            node["rotationQuat"] = ToJson(t.rotation_quat);
            node["useQuaternionRotation"] = t.use_quaternion_rotation;
            node["scale"] = ToJson(t.scale);
            return node;
        }

        [[nodiscard]] json WriteCamera(const SerializedCamera& c)
        {
            json node;
            node["projection"] = ProjectionToString(c.projection_mode);
            node["verticalFovRadians"] = c.vertical_fov_radians;
            node["aspectRatio"] = c.aspect_ratio;
            node["nearPlane"] = c.near_plane;
            node["farPlane"] = c.far_plane;
            node["orthographicHeight"] = c.orthographic_height;
            node["active"] = c.active;
            return node;
        }

        [[nodiscard]] json WriteRenderable(const SerializedRenderable& r)
        {
            json node;
            node["mesh"] = r.mesh;
            node["texture"] = r.texture;
            node["material"] = json{{"index", r.material_index}, {"generation", r.material_generation}};
            node["boundsCenter"] = ToJson(r.local_bounds.center);
            node["boundsExtents"] = ToJson(r.local_bounds.extents);
            node["visibilityLayer"] = r.visibility_layer;
            node["visible"] = r.visible;
            return node;
        }

        [[nodiscard]] json WriteLight(const SerializedLight& l)
        {
            json node;
            node["type"] = LightTypeToString(l.type);
            node["color"] = ToJson(l.color);
            node["intensity"] = l.intensity;
            node["localDirection"] = ToJson(l.local_direction);
            node["range"] = l.range;
            node["innerConeRadians"] = l.inner_cone_radians;
            node["outerConeRadians"] = l.outer_cone_radians;
            node["visibilityLayer"] = l.visibility_layer;
            node["visible"] = l.visible;
            node["editorEulerRadians"] = ToJson(l.editor_euler_radians);
            return node;
        }

        [[nodiscard]] json WritePhysics(const SerializedPhysics& p)
        {
            json node;
            node["mode"] = p.mode;
            node["shape"] = p.shape;
            node["boxHalfExtents"] = ToJson(p.box_half_extents);
            node["sphereRadius"] = p.sphere_radius;
            node["offset"] = ToJson(p.offset);
            node["gravityEnabled"] = p.gravity_enabled;
            node["continuousCollision"] = p.continuous_collision;
            return node;
        }

        // --- component readers -------------------------------------------------

        [[nodiscard]] SerializedTransform ReadTransform(const json& node)
        {
            SerializedTransform t;
            t.position = Vec3FromJson(node.value("position", json{}), t.position);
            t.rotation_euler_radians =
                Vec3FromJson(node.value("rotationEulerRadians", json{}), t.rotation_euler_radians);
            t.rotation_quat = QuatFromJson(node.value("rotationQuat", json{}), t.rotation_quat);
            t.use_quaternion_rotation = node.value("useQuaternionRotation", t.use_quaternion_rotation);
            t.scale = Vec3FromJson(node.value("scale", json{}), t.scale);
            return t;
        }

        [[nodiscard]] SerializedCamera ReadCamera(const json& node)
        {
            SerializedCamera c;
            c.projection_mode = ProjectionFromString(node.value("projection", std::string("Perspective")));
            c.vertical_fov_radians = node.value("verticalFovRadians", c.vertical_fov_radians);
            c.aspect_ratio = node.value("aspectRatio", c.aspect_ratio);
            c.near_plane = node.value("nearPlane", c.near_plane);
            c.far_plane = node.value("farPlane", c.far_plane);
            c.orthographic_height = node.value("orthographicHeight", c.orthographic_height);
            c.active = node.value("active", c.active);
            return c;
        }

        [[nodiscard]] SerializedRenderable ReadRenderable(const json& node)
        {
            SerializedRenderable r;
            r.mesh = node.value("mesh", r.mesh);
            r.texture = node.value("texture", r.texture);
            if (const auto material = node.find("material"); material != node.end() && material->is_object())
            {
                r.material_index = material->value("index", r.material_index);
                r.material_generation = material->value("generation", r.material_generation);
            }
            r.local_bounds.center = Vec3FromJson(node.value("boundsCenter", json{}), r.local_bounds.center);
            r.local_bounds.extents = Vec3FromJson(node.value("boundsExtents", json{}), r.local_bounds.extents);
            r.visibility_layer = node.value("visibilityLayer", r.visibility_layer);
            r.visible = node.value("visible", r.visible);
            return r;
        }

        [[nodiscard]] SerializedLight ReadLight(const json& node)
        {
            SerializedLight l;
            l.type = LightTypeFromString(node.value("type", std::string("Directional")));
            l.color = Vec3FromJson(node.value("color", json{}), l.color);
            l.intensity = node.value("intensity", l.intensity);
            l.local_direction = Vec3FromJson(node.value("localDirection", json{}), l.local_direction);
            l.range = node.value("range", l.range);
            l.inner_cone_radians = node.value("innerConeRadians", l.inner_cone_radians);
            l.outer_cone_radians = node.value("outerConeRadians", l.outer_cone_radians);
            l.visibility_layer = node.value("visibilityLayer", l.visibility_layer);
            l.visible = node.value("visible", l.visible);
            l.editor_euler_radians =
                Vec3FromJson(node.value("editorEulerRadians", json{}), l.editor_euler_radians);
            return l;
        }

        [[nodiscard]] SerializedPhysics ReadPhysics(const json& node)
        {
            SerializedPhysics p;
            p.mode = node.value("mode", p.mode);
            p.shape = node.value("shape", p.shape);
            p.box_half_extents = Vec3FromJson(node.value("boxHalfExtents", json{}), p.box_half_extents);
            p.sphere_radius = node.value("sphereRadius", p.sphere_radius);
            p.offset = Vec3FromJson(node.value("offset", json{}), p.offset);
            p.gravity_enabled = node.value("gravityEnabled", p.gravity_enabled);
            p.continuous_collision = node.value("continuousCollision", p.continuous_collision);
            return p;
        }
    }

    std::string WriteSceneToJson(const SerializedScene& scene)
    {
        json root;
        root["sceneVersion"] = kCurrentSceneVersion;
        root["generator"] = "RAG Engine";

        json entities = json::array();
        for (const SerializedEntity& entity : scene.entities)
        {
            json node;
            node["name"] = entity.name;
            if (entity.transform.has_value())
            {
                node["transform"] = WriteTransform(*entity.transform);
            }
            if (entity.camera.has_value())
            {
                node["camera"] = WriteCamera(*entity.camera);
            }
            if (entity.renderable.has_value())
            {
                node["renderable"] = WriteRenderable(*entity.renderable);
            }
            if (entity.light.has_value())
            {
                node["light"] = WriteLight(*entity.light);
            }
            if (entity.physics.has_value())
            {
                node["physics"] = WritePhysics(*entity.physics);
            }
            entities.push_back(std::move(node));
        }
        root["entities"] = std::move(entities);

        return root.dump(2);
    }

    bool ParseSceneFromJson(const std::string& text, SerializedScene& out_scene, std::string& out_error)
    {
        json root;
        try
        {
            root = json::parse(text);
        }
        catch (const std::exception& exception)
        {
            out_error = std::string("JSON parse error: ") + exception.what();
            return false;
        }

        if (!root.is_object())
        {
            out_error = "Scene file root is not a JSON object.";
            return false;
        }

        const auto version_node = root.find("sceneVersion");
        if (version_node == root.end() || !version_node->is_number_integer())
        {
            out_error = "Scene file is missing an integer \"sceneVersion\" field.";
            return false;
        }

        const int version = version_node->get<int>();
        if (version < kMinimumSupportedSceneVersion || version > kCurrentSceneVersion)
        {
            std::ostringstream message;
            message << "Unsupported sceneVersion " << version << " (this build supports "
                    << kMinimumSupportedSceneVersion << ".." << kCurrentSceneVersion << ").";
            out_error = message.str();
            return false;
        }

        out_scene = SerializedScene{};
        out_scene.version = version;

        const auto entities = root.find("entities");
        if (entities == root.end() || !entities->is_array())
        {
            out_error = "Scene file is missing an \"entities\" array.";
            return false;
        }

        try
        {
            for (const json& node : *entities)
            {
                if (!node.is_object())
                {
                    continue;
                }

                SerializedEntity entity;
                entity.name = node.value("name", std::string());
                if (const auto it = node.find("transform"); it != node.end() && it->is_object())
                {
                    entity.transform = ReadTransform(*it);
                }
                if (const auto it = node.find("camera"); it != node.end() && it->is_object())
                {
                    entity.camera = ReadCamera(*it);
                }
                if (const auto it = node.find("renderable"); it != node.end() && it->is_object())
                {
                    entity.renderable = ReadRenderable(*it);
                }
                if (const auto it = node.find("light"); it != node.end() && it->is_object())
                {
                    entity.light = ReadLight(*it);
                }
                if (const auto it = node.find("physics"); it != node.end() && it->is_object())
                {
                    entity.physics = ReadPhysics(*it);
                }
                out_scene.entities.push_back(std::move(entity));
            }
        }
        catch (const std::exception& exception)
        {
            out_error = std::string("Malformed entity data: ") + exception.what();
            return false;
        }

        return true;
    }

    bool SaveSceneToFile(
        const SerializedScene& scene,
        const std::filesystem::path& path,
        std::string& out_error)
    {
        std::error_code error;
        if (const std::filesystem::path parent = path.parent_path(); !parent.empty())
        {
            std::filesystem::create_directories(parent, error);
            if (error)
            {
                out_error = "Could not create directory '" + parent.string() + "': " + error.message();
                return false;
            }
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            out_error = "Could not open '" + path.string() + "' for writing.";
            return false;
        }

        stream << WriteSceneToJson(scene) << '\n';
        if (!stream.good())
        {
            out_error = "Failed while writing '" + path.string() + "'.";
            return false;
        }
        return true;
    }

    bool LoadSceneFromFile(
        const std::filesystem::path& path,
        SerializedScene& out_scene,
        std::string& out_error)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            out_error = "Could not open scene file '" + path.string() + "'.";
            return false;
        }

        std::ostringstream contents;
        contents << stream.rdbuf();
        return ParseSceneFromJson(contents.str(), out_scene, out_error);
    }
}
