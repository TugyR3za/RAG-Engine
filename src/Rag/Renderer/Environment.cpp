#include "Rag/Renderer/Environment.h"

#include <algorithm>
#include <string_view>

namespace rag::renderer
{
    bool ValidateEnvironment(const EnvironmentSettings& environment, std::string& error)
    {
        error.clear();
        if (!environment.skybox_enabled && std::all_of(
                environment.skybox_faces.begin(), environment.skybox_faces.end(),
                [](const std::string& path) { return path.empty(); }))
        {
            return true;
        }

        constexpr std::array names{"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
        for (std::size_t face = 0; face < environment.skybox_faces.size(); ++face)
        {
            const std::string& path = environment.skybox_faces[face];
            bool valid = !path.empty() && path.front() != '/' &&
                path.find_first_of("\\:") == std::string::npos &&
                std::none_of(path.begin(), path.end(), [](unsigned char c) { return c < 32; });
            std::size_t start = 0;
            while (valid && start <= path.size())
            {
                const std::size_t end = path.find('/', start);
                const std::string_view part(path.data() + start,
                    (end == std::string::npos ? path.size() : end) - start);
                valid = !part.empty() && part != "." && part != "..";
                if (end == std::string::npos) break;
                start = end + 1;
            }
            if (!valid)
            {
                error = "Skybox face " + std::string(names[face]) + " ('" + path +
                    "') must be a nonempty project-relative path using '/' without '.' or '..' segments.";
                return false;
            }
        }
        return true;
    }
}
