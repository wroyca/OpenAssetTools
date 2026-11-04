#include "ClipMapEntityDumperT5.h"

#include "Game/T5/Bsp/BspEntitiesT5.h"
#include "Game/T5/Bsp/EntityMathT5.h"
#include "Game/T5/CommonT5.h"
#include "Utils/Logging/Log.h"
#include "Utils/StringUtils.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <numbers>
#include <optional>
#include <vector>

using namespace T5;

namespace
{
    constexpr auto MAX_ULP_NUDGE = 3;
    constexpr auto MAX_POSITION_NUDGE = 4;
    constexpr auto MAX_DECIMALS = 6;
    constexpr auto MAX_DESCENT_STEP_BITS = 26;
    constexpr auto SPARE_DYN_ENTITY_COUNT = 256u;
    constexpr auto RAD_TO_DEG = 180.0 / std::numbers::pi;

    constexpr const char* DYN_ENTITY_TYPE_NAMES[]{"invalid", "clutter", "destruct"};

    std::string AssetName(const char* name)
    {
        if (!name)
            return std::string();
        if (name[0] == ',')
            return name + 1;

        return name;
    }

    float Nudge(const float value, const int steps)
    {
        auto result = value;
        for (auto i = 0; i < std::abs(steps); i++)
            result = std::nextafter(result, steps > 0 ? INFINITY : -INFINITY);

        return result;
    }

    void AddCandidate(std::vector<float>& candidates, const float value)
    {
        if (std::isfinite(value) && std::ranges::find(candidates, value) == candidates.end())
            candidates.emplace_back(value);
    }

    std::vector<float> DecimalCandidates(const double estimate)
    {
        std::vector<float> candidates;
        for (auto decimals = 0; decimals <= MAX_DECIMALS; decimals++)
        {
            const auto factor = std::pow(10.0, decimals);
            auto rounded = 0.0f;
            if (ParseBspFloat(std::format("{:.{}f}", std::round(estimate * factor) / factor, decimals), rounded))
                AddCandidate(candidates, rounded);
        }

        const auto nearest = static_cast<float>(estimate);
        for (auto step = 0; step <= MAX_ULP_NUDGE; step++)
        {
            AddCandidate(candidates, Nudge(nearest, step));
            AddCandidate(candidates, Nudge(nearest, -step));
        }

        return candidates;
    }

    std::vector<float> AngleCandidates(const double estimate)
    {
        auto candidates = DecimalCandidates(estimate);
        for (const auto wrapped : {estimate + 360.0, estimate - 360.0})
        {
            if (wrapped < -360.0 || wrapped > 360.0)
                continue;
            for (const auto candidate : DecimalCandidates(wrapped))
                AddCandidate(candidates, candidate);
        }

        return candidates;
    }

    std::vector<std::array<double, 3>> AxisToAngles(const double (&axis)[3][3])
    {
        const auto forwardLength = std::hypot(axis[0][0], axis[0][1]);
        const auto pitch = std::atan2(-axis[0][2], forwardLength) * RAD_TO_DEG;
        const auto yaw = std::atan2(axis[0][1], axis[0][0]) * RAD_TO_DEG;
        const auto roll = std::atan2(axis[1][2], axis[2][2]) * RAD_TO_DEG;

        std::vector<std::array<double, 3>> estimates{
            {pitch,         yaw,         roll        },
            {180.0 - pitch, yaw + 180.0, roll + 180.0},
        };
        if (forwardLength < 1e-6)
        {
            const auto combined = (pitch > 0.0 ? std::atan2(axis[1][0], axis[1][1]) : std::atan2(-axis[1][0], axis[1][1])) * RAD_TO_DEG;
            estimates.push_back({pitch, 0.0, combined});
            estimates.push_back({pitch, pitch > 0.0 ? -combined : combined, 0.0});
        }

        return estimates;
    }

    void QuatToAxis(const float* quat, double (&axis)[3][3])
    {
        const double x = quat[0];
        const double y = quat[1];
        const double z = quat[2];
        const double w = quat[3];

        axis[0][0] = 1.0 - 2.0 * (y * y + z * z);
        axis[0][1] = 2.0 * (x * y + w * z);
        axis[0][2] = 2.0 * (x * z - w * y);
        axis[1][0] = 2.0 * (x * y - w * z);
        axis[1][1] = 1.0 - 2.0 * (x * x + z * z);
        axis[1][2] = 2.0 * (y * z + w * x);
        axis[2][0] = 2.0 * (x * z + w * y);
        axis[2][1] = 2.0 * (y * z - w * x);
        axis[2][2] = 1.0 - 2.0 * (x * x + y * y);
    }

    std::vector<std::vector<float>> AngleCandidateSets(const double* estimate)
    {
        return {AngleCandidates(estimate[0]), AngleCandidates(estimate[1]), AngleCandidates(estimate[2])};
    }

    using AngleCost = std::function<uint64_t(const float*)>;

    int64_t OrderedBits(const float value)
    {
        const auto bits = std::bit_cast<int32_t>(value);
        return bits < 0 ? -static_cast<int64_t>(bits & 0x7FFFFFFF) - 1 : bits;
    }

    float FromOrderedBits(const int64_t bits)
    {
        if (bits < 0)
            return std::bit_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(-bits - 1) | 0x80000000u));

        return std::bit_cast<float>(static_cast<int32_t>(bits));
    }

    uint64_t UlpDistance(const float lhs, const float rhs)
    {
        const auto distance = OrderedBits(lhs) - OrderedBits(rhs);
        return static_cast<uint64_t>(distance < 0 ? -distance : distance);
    }

    std::optional<std::array<float, 3>> DescendAngles(const std::array<double, 3>& estimate, const AngleCost& cost)
    {
        int64_t bits[3]{
            OrderedBits(static_cast<float>(estimate[0])), OrderedBits(static_cast<float>(estimate[1])), OrderedBits(static_cast<float>(estimate[2]))};
        const auto evaluate = [&cost](const int64_t* candidateBits)
        {
            const float angles[3]{FromOrderedBits(candidateBits[0]), FromOrderedBits(candidateBits[1]), FromOrderedBits(candidateBits[2])};
            return cost(angles);
        };

        auto best = evaluate(bits);
        for (auto step = int64_t{1} << MAX_DESCENT_STEP_BITS; step >= 1 && best != 0u; step /= 2)
        {
            auto improved = true;
            while (improved && best != 0u)
            {
                improved = false;
                for (auto axis = 0; axis < 3; axis++)
                {
                    for (const auto direction : {-1, 1})
                    {
                        int64_t candidate[3]{bits[0], bits[1], bits[2]};
                        candidate[axis] += direction * step;
                        const auto candidateCost = evaluate(candidate);
                        if (candidateCost < best)
                        {
                            best = candidateCost;
                            std::ranges::copy(candidate, bits);
                            improved = true;
                        }
                    }
                }
            }
        }

        for (auto pitch = -MAX_ULP_NUDGE; pitch <= MAX_ULP_NUDGE && best != 0u; pitch++)
        {
            for (auto yaw = -MAX_ULP_NUDGE; yaw <= MAX_ULP_NUDGE && best != 0u; yaw++)
            {
                for (auto roll = -MAX_ULP_NUDGE; roll <= MAX_ULP_NUDGE && best != 0u; roll++)
                {
                    const int64_t candidate[3]{bits[0] + pitch, bits[1] + yaw, bits[2] + roll};
                    if (evaluate(candidate) == 0u)
                    {
                        std::ranges::copy(candidate, bits);
                        best = 0u;
                    }
                }
            }
        }

        if (best != 0u)
            return std::nullopt;

        return std::array{FromOrderedBits(bits[0]), FromOrderedBits(bits[1]), FromOrderedBits(bits[2])};
    }

    std::optional<std::array<float, 3>> FindAngles(const std::vector<std::array<double, 3>>& estimates, const AngleCost& cost, const bool descend = true)
    {
        for (const auto& estimate : estimates)
        {
            const auto candidates = AngleCandidateSets(estimate.data());
            for (const auto pitch : candidates[0])
            {
                for (const auto yaw : candidates[1])
                {
                    for (const auto roll : candidates[2])
                    {
                        const float angles[3]{pitch, yaw, roll};
                        if (cost(angles) == 0u)
                            return std::array{pitch, yaw, roll};
                    }
                }
            }
        }

        if (!descend)
            return std::nullopt;

        for (const auto& estimate : estimates)
        {
            if (const auto angles = DescendAngles(estimate, cost))
                return angles;
        }

        return std::nullopt;
    }

    std::optional<std::array<float, 3>> FindWorldPosition(const vec3_t& local, const float (&xform)[4][3])
    {
        double estimate[3];
        for (auto j = 0; j < 3; j++)
            estimate[j] = static_cast<double>(xform[3][j]) + local.v[0] * static_cast<double>(xform[0][j]) + local.v[1] * static_cast<double>(xform[1][j])
                          + local.v[2] * static_cast<double>(xform[2][j]);

        std::vector<float> candidates[3];
        for (auto j = 0; j < 3; j++)
        {
            candidates[j] = DecimalCandidates(estimate[j]);
            for (auto step = MAX_ULP_NUDGE + 1; step <= MAX_POSITION_NUDGE; step++)
            {
                AddCandidate(candidates[j], Nudge(static_cast<float>(estimate[j]), step));
                AddCandidate(candidates[j], Nudge(static_cast<float>(estimate[j]), -step));
            }
        }

        for (const auto x : candidates[0])
        {
            for (const auto y : candidates[1])
            {
                for (const auto z : candidates[2])
                {
                    const float world[3]{x, y, z};
                    float result[3];
                    MatrixTransposeTransformVector43(world, xform, result);
                    if (result[0] == local.v[0] && result[1] == local.v[1] && result[2] == local.v[2])
                        return std::array{x, y, z};
                }
            }
        }

        return std::nullopt;
    }

    std::optional<float> FindProductFactor(const float product, const float factor)
    {
        if (factor == 0.0f)
            return product;

        for (const auto candidate : DecimalCandidates(static_cast<double>(product) / static_cast<double>(factor)))
        {
            if (candidate != 0.0f && candidate * factor == product)
                return candidate;
        }

        return std::nullopt;
    }

    std::optional<float> FindTimeout(const int timeout)
    {
        for (const auto candidate : DecimalCandidates(timeout / 1000.0))
        {
            if (GetConstraintTimeout(candidate) == timeout)
                return candidate;
        }

        return std::nullopt;
    }

    bool IsRemovedClass(const std::string& className)
    {
        for (const auto* removed :
             {"misc_model", "dyn_model", "dyn_brushmodel", "info_null", "dyn_pointconstraint", "dyn_hingeconstraint", "dyn_lightconstraint", "rope"})
        {
            if (utils::StringEqualsIgnoreCase(className, removed))
                return true;
        }

        return false;
    }

    bool SamePosition(const float* lhs, const vec3_t& rhs)
    {
        return lhs[0] == rhs.v[0] && lhs[1] == rhs.v[1] && lhs[2] == rhs.v[2];
    }

    struct EntityTransform
    {
        std::string targetname;
        float xform[4][3];
    };

    class ClipMapEntityDumper
    {
    public:
        ClipMapEntityDumper(const clipMap_t& clipMap, const Zone& zone)
            : m_clip_map(clipMap),
              m_zone(zone)
        {
        }

        std::string Dump()
        {
            LoadBaseEntities();
            DumpDynEntities();
            DumpConstraints();
            DumpStaticModels();

            std::vector<BspEntity> entities = std::move(m_base_entities);
            for (auto* list : {&m_constraint_entities, &m_info_entities, &m_dyn_entities, &m_static_model_entities})
                entities.insert(entities.end(), list->begin(), list->end());

            return WriteBspEntities(entities);
        }

    private:
        [[nodiscard]] std::string ScriptString(const uint16_t value) const
        {
            if (value >= m_zone.m_script_strings.Count())
                return std::string();

            return m_zone.m_script_strings[value];
        }

        void LoadBaseEntities()
        {
            if (!m_clip_map.mapEnts || !m_clip_map.mapEnts->entityString)
                return;

            const auto entities = ParseBspEntities(m_clip_map.mapEnts->entityString);
            if (!entities)
            {
                con::warn("Could not parse map ents of clipMap \"{}\"", m_clip_map.name);
                return;
            }

            for (const auto& entity : *entities)
            {
                if (IsRemovedClass(entity.GetString("classname")))
                    continue;

                if (utils::StringEqualsIgnoreCase(entity.GetString("classname"), "script_model")
                    || utils::StringEqualsIgnoreCase(entity.GetString("classname"), "script_brushmodel"))
                {
                    float origin[3]{};
                    float angles[3]{};
                    if (const auto* value = entity.GetValue("origin"))
                        (void)ParseBspVec3(*value, origin);
                    if (const auto* value = entity.GetValue("angles"))
                        (void)ParseBspVec3(*value, angles);

                    auto& transform = m_ent_transforms.emplace_back();
                    transform.targetname = entity.GetString("targetname");
                    AnglesToTransform(origin, angles, transform.xform);
                }

                m_base_entities.emplace_back(entity);
            }
        }

        [[nodiscard]] std::string SoundName(const unsigned hash) const
        {
            for (const auto* bankInfo : m_zone.m_pools.PoolAssets<AssetSoundBank>())
            {
                const auto* bank = bankInfo->Asset();
                for (auto i = 0u; i < bank->aliasCount; i++)
                {
                    const auto* name = bank->alias[i].name;
                    if (name && Common::SND_HashName(name) == hash)
                        return name;
                }
            }

            return std::format("@{:x}", hash);
        }

        [[nodiscard]] std::vector<const PhysConstraint*> AttachedConstraints(const DynEntityDef& def, const int absId) const
        {
            std::vector<const PhysConstraint*> attached;
            for (auto i = 0; i < m_clip_map.num_constraints; i++)
            {
                const auto& constraint = m_clip_map.constraints[i];
                if (constraint.attach_point_type1 == ATTACH_POINT_DYNENT && constraint.target_ent1 == def.targetname && constraint.target_index1 == absId)
                    attached.emplace_back(&constraint);
            }

            return attached;
        }

        void DumpDynEntity(const DynEntityDef& def, const int absId)
        {
            BspEntity entity;
            entity.SetValue("classname", def.xModel ? "dyn_model" : "dyn_brushmodel");
            if (static_cast<unsigned>(def.type) < std::size(DYN_ENTITY_TYPE_NAMES))
                entity.SetValue("type", DYN_ENTITY_TYPE_NAMES[def.type]);
            entity.SetValue("model", def.xModel ? AssetName(def.xModel->name) : std::format("*{}", def.brushModel));
            if (def.physicsBrushModel != 0u)
                entity.SetValue("physicsmodel", std::format("*{}", def.physicsBrushModel));
            if (def.destroyedxModel)
                entity.SetValue("destroyedModel", AssetName(def.destroyedxModel->name));
            if (def.destroyFx)
                entity.SetValue("destroyEfx", AssetName(def.destroyFx->name));
            if (def.destroySound != 0u)
                entity.SetValue("destroySound", SoundName(def.destroySound));
            if (def.destroyPieces)
                con::warn("Dyn entity pieces \"{}\" of clipMap \"{}\" are not dumped", def.destroyPieces->name ? def.destroyPieces->name : "", m_clip_map.name);
            if (def.physPreset)
            {
                if (def.type == DYNENT_TYPE_CLUTTER)
                    entity.SetValue("physPreset", AssetName(def.physPreset->name));
                else
                    con::warn("Dyn entity physics preset of clipMap \"{}\" cannot be dumped for its type", m_clip_map.name);
            }

            const auto targetname = ScriptString(def.targetname);
            const auto target = ScriptString(def.target);
            if (!targetname.empty())
                entity.SetValue("targetname", targetname);
            if (!target.empty())
                entity.SetValue("target", target);

            entity.SetValue("origin", FormatBspVec3(def.pose.origin));

            double axis[3][3];
            QuatToAxis(def.pose.quat, axis);
            const auto estimates = AxisToAngles(axis);
            const auto quatCost = [&def](const float* candidate)
            {
                float quat[4];
                AnglesToQuat(candidate, quat);

                auto cost = 0ull;
                for (auto i = 0; i < 4; i++)
                    cost += UlpDistance(quat[i], def.pose.quat[i]);

                return cost;
            };

            const auto attached = AttachedConstraints(def, absId);
            auto angles = FindAngles(estimates,
                                     [&](const float* candidate)
                                     {
                                         auto cost = quatCost(candidate);

                                         float xform[4][3];
                                         AnglesToTransform(def.pose.origin, candidate, xform);
                                         for (const auto* constraint : attached)
                                         {
                                             float local[3];
                                             MatrixTransposeTransformVector43(constraint->pos2.v, xform, local);
                                             for (auto i = 0; i < 3; i++)
                                                 cost += UlpDistance(local[i], constraint->pos.v[i]);
                                         }

                                         return cost;
                                     });
            if (!angles && !attached.empty())
                angles = FindAngles(estimates, quatCost);

            auto& transform = m_dyn_transforms.emplace_back();
            transform.targetname = targetname;
            if (angles)
            {
                entity.SetValue("angles", FormatBspVec3(angles->data()));
                AnglesToTransform(def.pose.origin, angles->data(), transform.xform);
            }
            else
            {
                con::warn("Could not find angles for dyn entity quat of clipMap \"{}\"", m_clip_map.name);
                const float fallback[3]{static_cast<float>(estimates[0][0]), static_cast<float>(estimates[0][1]), static_cast<float>(estimates[0][2])};
                entity.SetValue("angles", FormatBspVec3(fallback));
                AnglesToTransform(def.pose.origin, fallback, transform.xform);
            }

            if (def.health != 0 && !(def.destroyedxModel && def.health == 1))
                entity.SetValue("health", std::to_string(def.health));
            if ((def.flags & 0x1F) != 0)
                entity.SetValue("spawnflags", std::to_string(def.flags & 0x1F));

            m_dyn_entities.emplace_back(std::move(entity));
        }

        void DumpDynEntities()
        {
            for (auto i = 0u; i < m_clip_map.dynEntCount[DYNENT_DRAW_BRUSH]; i++)
                DumpDynEntity(m_clip_map.dynEntDefList[DYNENT_DRAW_BRUSH][i], static_cast<int>(m_clip_map.originalDynEntCount + SPARE_DYN_ENTITY_COUNT + i));

            for (auto i = 0u; i < m_clip_map.originalDynEntCount; i++)
                DumpDynEntity(m_clip_map.dynEntDefList[DYNENT_DRAW_MODEL][i], static_cast<int>(i));
        }

        [[nodiscard]] const EntityTransform* FindAttachTransform(const AttachPointType attachType, const std::string& targetname) const
        {
            const auto* transforms = attachType == ATTACH_POINT_DYNENT ? &m_dyn_transforms : attachType == ATTACH_POINT_ENT ? &m_ent_transforms : nullptr;
            if (!transforms)
                return nullptr;

            const EntityTransform* result = nullptr;
            for (const auto& transform : *transforms)
            {
                if (utils::StringEqualsIgnoreCase(transform.targetname, targetname))
                    result = &transform;
            }

            return result;
        }

        std::string AddInfoNode(const float* origin, const std::string& target)
        {
            auto name = std::format("{}_constraint_node{}", AssetName(m_clip_map.name), m_info_entities.size());
            std::ranges::replace(name, '/', '_');

            BspEntity info;
            info.SetValue("classname", "info_null");
            info.SetValue("targetname", name);
            if (!target.empty())
                info.SetValue("target", target);
            info.SetValue("origin", FormatBspVec3(origin));
            m_info_entities.emplace_back(std::move(info));

            return name;
        }

        std::string DumpConstraintSide(const PhysConstraint& constraint, const bool second, const float* origin)
        {
            const auto attachType = second ? constraint.attach_point_type2 : constraint.attach_point_type1;
            const auto& pos = second ? constraint.pos2 : constraint.pos;

            if (attachType == ATTACH_POINT_WORLD)
            {
                if (SamePosition(origin, pos))
                    return std::string();
                if (constraint.type == CONSTRAINT_ROPE)
                    return AddInfoNode(pos.v, std::string());

                con::warn("Constraint position of clipMap \"{}\" differs from its origin", m_clip_map.name);
                return std::string();
            }

            const auto targetname = ScriptString(second ? constraint.target_ent2 : constraint.target_ent1);
            const auto* transform = FindAttachTransform(attachType, targetname);
            if (!transform)
            {
                con::warn("Constraint target \"{}\" of clipMap \"{}\" was not found", targetname, m_clip_map.name);
                return targetname;
            }

            float local[3];
            MatrixTransposeTransformVector43(origin, transform->xform, local);
            if (SamePosition(local, pos))
                return targetname;

            const auto world = FindWorldPosition(pos, transform->xform);
            if (!world)
                con::warn("Could not find a world position for a constraint of clipMap \"{}\"", m_clip_map.name);

            return AddInfoNode(world ? world->data() : origin, targetname);
        }

        [[nodiscard]] const char* ConstraintClassName(const ConstraintType type) const
        {
            switch (type)
            {
            case CONSTRAINT_POINT:
                return "dyn_pointconstraint";
            case CONSTRAINT_HINGE:
                return "dyn_hingeconstraint";
            case CONSTRAINT_ROPE:
                return "rope";
            case CONSTRAINT_LIGHT:
                return "dyn_lightconstraint";
            default:
                con::warn("Constraint type {} of clipMap \"{}\" cannot be dumped", static_cast<int>(type), m_clip_map.name);
                return "dyn_pointconstraint";
            }
        }

        [[nodiscard]] std::optional<std::array<float, 3>> GetAttachPoint(const AttachPointType attachType, const uint16_t targetEnt, const vec3_t& pos) const
        {
            if (attachType == ATTACH_POINT_WORLD)
                return std::array{pos.v[0], pos.v[1], pos.v[2]};

            const auto* transform = FindAttachTransform(attachType, ScriptString(targetEnt));
            if (!transform)
                return std::nullopt;

            std::array<float, 3> point{};
            MatrixTransformVector43(pos.v, transform->xform, point.data());
            return point;
        }

        void DumpRopeLength(const PhysConstraint& constraint, BspEntity& entity) const
        {
            const auto p0 = GetAttachPoint(constraint.attach_point_type1, constraint.target_ent1, constraint.pos);
            const auto p1 = GetAttachPoint(constraint.attach_point_type2, constraint.target_ent2, constraint.pos2);
            if (p0 && p1)
            {
                auto distance = GetAttachPointDistance(p0->data(), p1->data());
                if (constraint.scale.y != 0.0f)
                    distance = distance * constraint.scale.y;
                if (distance == constraint.distance)
                    return;
            }

            const auto length = FindProductFactor(constraint.distance, constraint.scale.y);
            if (!length)
                con::warn("Could not find a rope length for clipMap \"{}\"", m_clip_map.name);
            entity.SetValue("length", FormatBspFloat(length.value_or(constraint.distance)));
        }

        void DumpConstraint(const PhysConstraint& constraint)
        {
            BspEntity entity;
            entity.SetValue("classname", ConstraintClassName(constraint.type));

            const auto targetname = ScriptString(constraint.targetname);
            if (!targetname.empty())
                entity.SetValue("targetname", targetname);

            const auto* origin = constraint.pos2.v;
            if (constraint.attach_point_type1 == ATTACH_POINT_WORLD
                && (constraint.type == CONSTRAINT_ROPE || constraint.attach_point_type2 != ATTACH_POINT_WORLD))
                origin = constraint.pos.v;
            entity.SetValue("origin", FormatBspVec3(origin));

            const auto target = DumpConstraintSide(constraint, false, origin);
            const auto target2 = constraint.type == CONSTRAINT_LIGHT ? std::to_string(constraint.target_index2) : DumpConstraintSide(constraint, true, origin);
            if (!target.empty())
                entity.SetValue("target", target);
            if (!target2.empty())
                entity.SetValue("target2", target2);

            if (constraint.dir.x != 0.0f || constraint.dir.y != 0.0f || constraint.dir.z != 0.0f)
                entity.SetValue("angles", FormatBspVec3(constraint.dir.v));
            if (constraint.min_health != 0)
                entity.SetValue("health", std::to_string(constraint.min_health));
            if (constraint.damp != 0.0f)
                entity.SetValue("damp", FormatBspFloat(constraint.damp));
            if (constraint.scale.x != 0.0f)
                entity.SetValue("force_scale", FormatBspFloat(constraint.scale.x));
            if (constraint.scale.y != 0.0f)
                entity.SetValue("length_scale", FormatBspFloat(constraint.scale.y));

            if (constraint.type == CONSTRAINT_HINGE)
            {
                entity.SetValue("min_angle", FormatBspFloat(constraint.minAngle));
                entity.SetValue("max_angle", FormatBspFloat(constraint.maxAngle));
            }

            if (constraint.type == CONSTRAINT_ROPE)
            {
                if (constraint.minAngle != 0.0f)
                    entity.SetValue("width", FormatBspFloat(constraint.minAngle));
                if (constraint.material)
                    entity.SetValue("material", AssetName(constraint.material->info.name));
                if (constraint.flags & 1)
                    entity.SetValue("spawnflags", "1");
                DumpRopeLength(constraint, entity);
            }
            else if (constraint.distance != 0.0f)
                entity.SetValue("length", FormatBspFloat(constraint.distance));

            if (constraint.timeout != 0)
            {
                const auto timeout = FindTimeout(constraint.timeout);
                if (!timeout)
                    con::warn("Could not find a constraint timeout for clipMap \"{}\"", m_clip_map.name);
                entity.SetValue("timeout", FormatBspFloat(timeout.value_or(static_cast<float>(constraint.timeout) / 1000.0f)));
            }

            m_constraint_entities.emplace_back(std::move(entity));
        }

        void DumpConstraints()
        {
            for (auto i = 0; i < m_clip_map.num_constraints; i++)
                DumpConstraint(m_clip_map.constraints[i]);
        }

        void DumpStaticModel(const cStaticModel_s& staticModel)
        {
            BspEntity entity;
            entity.SetValue("classname", "misc_model");
            entity.SetValue("model", AssetName(staticModel.xmodel->name));
            entity.SetValue("origin", FormatBspVec3(staticModel.origin));

            double invScale = 0.0;
            for (auto i = 0; i < 3; i++)
                invScale += static_cast<double>(staticModel.invScaledAxis[i][0]) * staticModel.invScaledAxis[i][0];
            invScale = std::sqrt(invScale);

            double axis[3][3];
            for (auto i = 0; i < 3; i++)
            {
                for (auto j = 0; j < 3; j++)
                    axis[j][i] = staticModel.invScaledAxis[i][j] / invScale;
            }

            const auto estimates = AxisToAngles(axis);

            const auto scales = DecimalCandidates(1.0 / invScale);
            for (const auto descend : {false, true})
            {
                for (const auto scale : scales)
                {
                    const auto angles = FindAngles(
                        estimates,
                        [&staticModel, scale](const float* candidate)
                        {
                            float candidateAxis[3][3];
                            AnglesToAxis(candidate, candidateAxis);
                            const auto candidateInvScale = 1.0f / scale;

                            auto cost = 0ull;
                            for (auto i = 0; i < 3; i++)
                            {
                                for (auto j = 0; j < 3; j++)
                                    cost += UlpDistance(candidateInvScale * candidateAxis[j][i], staticModel.invScaledAxis[i][j]);
                            }
                            if (cost != 0u || !staticModel.xmodel->collSurfs)
                                return cost;

                            cStaticModel_s test{};
                            test.xmodel = staticModel.xmodel;
                            InitStaticModel(test, staticModel.origin, candidate, scale);
                            for (auto i = 0; i < 3; i++)
                                cost += UlpDistance(test.absmin[i], staticModel.absmin[i]) + UlpDistance(test.absmax[i], staticModel.absmax[i]);

                            return cost;
                        },
                        descend);
                    if (!angles)
                        continue;

                    entity.SetValue("angles", FormatBspVec3(angles->data()));
                    if (scale != 1.0f)
                        entity.SetValue("modelscale", FormatBspFloat(scale));
                    m_static_model_entities.emplace_back(std::move(entity));
                    return;
                }
            }

            con::warn("Could not find angles and scale for static model \"{}\" of clipMap \"{}\"", AssetName(staticModel.xmodel->name), m_clip_map.name);
            const float fallback[3]{static_cast<float>(estimates[0][0]), static_cast<float>(estimates[0][1]), static_cast<float>(estimates[0][2])};
            entity.SetValue("angles", FormatBspVec3(fallback));
            if (std::abs(1.0 / invScale - 1.0) > 1e-6)
                entity.SetValue("modelscale", FormatBspFloat(static_cast<float>(1.0 / invScale)));
            m_static_model_entities.emplace_back(std::move(entity));
        }

        void DumpStaticModels()
        {
            for (auto i = 0u; i < m_clip_map.numStaticModels; i++)
                DumpStaticModel(m_clip_map.staticModelList[i]);
        }

        const clipMap_t& m_clip_map;
        const Zone& m_zone;
        std::vector<BspEntity> m_base_entities;
        std::vector<BspEntity> m_constraint_entities;
        std::vector<BspEntity> m_info_entities;
        std::vector<BspEntity> m_dyn_entities;
        std::vector<BspEntity> m_static_model_entities;
        std::vector<EntityTransform> m_ent_transforms;
        std::vector<EntityTransform> m_dyn_transforms;
    };
} // namespace

namespace clip_map
{
    std::string DumpEntitiesT5(const clipMap_t& clipMap, const Zone& zone)
    {
        ClipMapEntityDumper dumper(clipMap, zone);
        return dumper.Dump();
    }
} // namespace clip_map
