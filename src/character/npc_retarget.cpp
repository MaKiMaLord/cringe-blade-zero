Y��x-���jם��i��+��j[h��ܢ��߭tӽ��vo+^����ם﻿#include "npc_display_pool.h"
#include "npc_skill_support.h"
#include "npc_retarget.h"
#include "bone_motion.h"
#include "npc_source_budget.h"
#include "npc_cutscene_policy.h"
#include "combo_playback_rules.h"
#include "combo_hold_scope.h"
#include "view_follow_state.h"
#include "trim_charge_runtime.h"
#include "ue_runtime.h"
#include "npc_grip_math.h"
#include "npc_grip_routes.h"
#include "npc_body_collision.h"
#include "npc_body.h"
#include "npc_bridge.h"
#include "npc_pose_transfer.h"
#include "npc_own_locomotion.h"
#include "npc_hero_size.h"
#include "npc_fx_lookup.h"
#include "grab_extras.h"
#include "npc_boss_effects.h"
#include "skill_toast.h"
#include "ui_hotkeys.h"
#include "settings_store.h"
#include "entry_protection.h"
#include "motian_grab.h"
#include "grab_boss.h"
#include "npc_combo.h"
#include "combo_weapons.h"
#include "weapon_presets.h"
#include "air_skills.h"
#include "boss_assist.h"
#include "move_preview.h"
#include "test_dummy.h"
#include "combo_plugins.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace NpcRetarget {
namespace {
using ue::Object; using ue::Call; using ue::Name; using ue::Array;
using Transform=NpcPoseTransfer::Transform;
struct Vector { double x = 0, y = 0, z = 0; };
static_assert(sizeof(Transform) == 96 && sizeof(Vector) == 24);
struct SoftPath { int32_t index = 0, serial = 0; Name package, asset; ue::String sub; };
static_assert(sizeof(SoftPath) == 40);
struct BoneFallback { const char* bone; const char* ancestor; Transform relative; };
struct GripBasis { const char* bone; const char* hand; Transform source_to_hand, target_to_hand; };
struct GripRemapBasis { const char* profile; const char* bone; double q0[4]; Transform target_mesh_ref,source_palm_hand,target_palm_hand_ref; double hand_basis[4]; const char* palm; };
#include "npc_grip_remap.generated.h"
struct ForearmBasis { const char* profile;const char* bone;const char* actor_class;const char* mesh;double rotation[4]; };
#include "npc_forearm_basis.generated.h"
// mode: the bone's cooked EBoneTranslationRetargetingMode on that Skeleton; length: its reference translation length;
// animated: that Skeleton's animations carry the bone (the engine fills an untracked one with the mesh rest).
struct BodySkeletonBasis { const char* bone;float q0[4];float length;uint16_t skeleton;uint8_t mode,animated; };  // skeleton: kBodySkeletonNames index
static_assert(sizeof(BodySkeletonBasis)==32);
#include "npc_body_basis.generated.h"
struct Profile {
    const char* id; const wchar_t* name; const char* donor; const char* body; const char* skeleton; int32_t bones;
    const uint16_t* shared; size_t shared_count; const char* const* parts; size_t part_count;
    const BoneFallback* fallbacks; size_t fallback_count; const GripBasis* grips; size_t grip_count;
    const char* head; const char* head_postprocess; const uint16_t* facial_bones; size_t facial_count; int32_t head_bones; const wchar_t* detail;
};
#include "npc_retarget_data.generated.h"
#include "npc_weapon_routes.generated.h"
#include "npc_skills.generated.h"
static_assert(std::size(kNpcSkillSets)==std::size(kProfiles),"npc_skills.generated.h rows follow kProfiles");
#include "npc_skill_colliders.generated.h"
static_assert(std::size(kNpcColliderTagSets)==std::size(kNpcSkillSets),"npc_skill_colliders.generated.h rows follow kNpcSkillSets");
#include "npc_weapon_displays.generated.h"
#include "npc_dodges.generated.h"
#include "npc_self_kill.generated.h"
static_assert(std::size(kNpcDodgeSets)==std::size(kProfiles),"npc_dodges.generated.h rows follow kProfiles");
static_assert(std::size(kNpcWeaponDisplays)==std::size(kNpcSkillSets),"npc_weapon_displays.generated.h rows follow kNpcSkillSets");
#include "npc_chains.generated.h"
#include "npc_skill_trails.generated.h"
static_assert(std::size(kNpcTrailConfigSets)==std::size(kNpcSkillSets),"npc_skill_trails.generated.h rows follow kNpcSkillSets");
#include "npc_weapon_sets.generated.h"
static_assert(std::size(kNpcWeaponSetsBySkin)==std::size(kNpcSkillSets),"npc_weapon_sets.generated.h rows follow kNpcSkillSets");
// A second creature under the display Skeleton root that the hero has no bones for (mount_root, bones in its
// subtree) and the boss's own locomotion loops; ridden: the rider sits on it (a horse).
struct MountLoops { const char* skeleton;const char* mount_root;int32_t bones;uint8_t ridden;const char* idle;const char* walk;const char* run; };
#include "npc_mounts.generated.h"
// Each look's own idle / walk / run (rows follow kProfiles): what its pose puppet plays (NpcOwnLocomotion).
struct OwnLocomotion { const char* profile;const char* idle;const char* walk[4];const char* run[4]; };
#include "npc_locomotion.generated.h"
static_assert(std::size(kOwnLocomotion)==std::size(kProfiles),"npc_locomotion.generated.h rows follow kProfiles");
constexpr uintptr_t kArrayAddRva = 0x0406ce30, kArrayRemoveRva = 0x0406db20;
// Set only after exact-PE proof. Array edits alone do not update Shipping SocketMap.
constexpr uintptr_t kRebuildSocketMapRva = 0x044fc870;
constexpr uintptr_t kSocketTransformRva = 0x03db50e0;
constexpr uintptr_t kSocketInfoRva = 0x03db49c0;
constexpr uintptr_t kGetMeshReferenceRva=0x044f5ae0;
constexpr size_t kSocketTransformSlot = 0x518 / sizeof(uintptr_t);
struct NativeSignature { uintptr_t rva; unsigned char code[16]; };
constexpr NativeSignature kNativeSignatures[] = {  // exe-pins
    {kArrayAddRva, {0x48,0x8b,0xc4,0x53,0x56,0x41,0x55,0x48,0x83,0xec,0x50,0x4d,0x8b,0xe8,0x48,0x8b}},
    {kArrayRemoveRva, {0x48,0x85,0xc9,0x0f,0x84,0xae,0x01,0x00,0x00,0x48,0x89,0x6c,0x24,0x18,0x48,0x89}},
    {kRebuildSocketMapRva, {0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8d,0x6c,0x24,0xc9,0x48,0x81,0xec,0xe0}},
    {kSocketTransformRva, {0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24}},
    {kSocketInfoRva, {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x41,0x56,0x41}},
    {kGetMeshReferenceRva,{0x40,0x53,0x48,0x83,0xec,0x20,0xba,0x04,0x00,0x00,0x00,0x41,0xb8,0x03,0x00,0x00}},
    {0x1267790, {0x48,0x8b,0x05,0xb1,0x10,0xaa,0x0c,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}},
    {0x1f27810, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x81,0xec,0x80,0x00}},
    {0x3da9320, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48}},
    {0x5cc9a30, {0x48,0x89,0x5c,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0x50,0x10,0x00}},
};
// Grooms are bound to the head but culled by their own bounds; 4x covers crouch, roll and lunge poses.
constexpr float kHairBoundsScale = 4.0f, kNoPhysicsBoundsScale = 2.0f;
constexpr const char* kSourceSkeleton = "/HuaShira/Characters/Hero/Hero_Hun/Hun_Skin_Skeleton.Hun_Skin_Skeleton";
// Hun_Skin_Skeleton bone translation retarget modes (cooked BoneTree, character_swap/reference_bones.json):
// these 65 are Animation, ball_l / ball_r Skeleton, the other 1067 AnimationScaled.
constexpr const char* kSourceAnimationBones[] = {
    "hand_l","pinky_metacarpal_l","pinky_01_l","pinky_02_l","pinky_03_l","ring_metacarpal_l","ring_01_l","ring_02_l","ring_03_l",
    "middle_metacarpal_l","middle_01_l","middle_02_l","middle_03_l","index_metacarpal_l","index_01_l","index_02_l","index_03_l",
    "thumb_01_l","thumb_02_l","thumb_03_l","wrist_inner_l","wrist_outer_l","weapon_l","weapon_l_Inst","hand_r","pinky_metacarpal_r",
    "pinky_01_r","pinky_02_r","pinky_03_r","ring_metacarpal_r","ring_01_r","ring_02_r","ring_03_r","middle_metacarpal_r","middle_01_r",
    "middle_02_r","middle_03_r","index_metacarpal_r","index_01_r","index_02_r","index_03_r","thumb_01_r","thumb_02_r","thumb_03_r",
    "wrist_inner_r","wrist_outer_r","weapon_r","weapon_r_Inst","ik_foot_root","ik_foot_l","ik_foot_r","ik_hand_root","ik_hand_gun",
    "ik_hand_l","ik_hand_r","interaction","center_of_mass","Pt_Cam","ik_weapon_root","ik_weapon_l","ik_weapon_r","weapon_l_child",
    "weapon_r_child","ik_weapon_l_child","ik_weapon_r_child"};
constexpr const char* kSourceSkeletonBones[] = {"ball_l","ball_r"};
SRWLOCK snapshot_lock = SRWLOCK_INIT, request_lock = SRWLOCK_INIT;
Snapshot snapshot_;
Logger logger_ = nullptr;
ReleaseAppearance release_appearance_ = nullptr;
Object* engine_ = nullptr;
bool initialized_ = false, active_ = false, cleanup_pending_ = false, primary_restored_ = false, primary_restore_started_ = false;
bool source_driver_mode_=false,source_settings_owned_=false,source_visible_written_=false,source_hidden_written_=false;
volatile LONG disabled_ = 0;
volatile LONG experimental_ = 1; // on by default since the skins left the experiment (11-05); npcretarget experimental off still restores and blocks
int32_t pending_action_ = -1, pending_entry_ = -1, waiting_action_ = -1, waiting_entry_ = -1, selected_ = -1;
ULONGLONG next_tick_ = 0, wait_deadline_ = 0;
DWORD faults_ = 0, fault_code_ = 0;
uintptr_t fault_address_ = 0;
const char* stage_ = "idle";
std::wstring failure_text_;

enum class PerfKind : size_t { BodyPre, FacePre, BodyCache, Grip, ForearmPoll, Maintenance, Count };
struct PerfBucket { volatile LONG64 calls=0,total=0,maximum=0; };
std::array<PerfBucket,static_cast<size_t>(PerfKind::Count)> perf_{};
LONGLONG perf_frequency_=0;ULONGLONG perf_next_report_=0;volatile LONG perf_enabled_=0;
LONGLONG PerfStart(){if(!InterlockedCompareExchange(&perf_enabled_,0,0))return 0;LARGE_INTEGER t{};QueryPerformanceCounter(&t);return t.QuadPart;}
void PerfEnd(PerfKind kind,LONGLONG start){if(!start||!InterlockedCompareExchange(&perf_enabled_,0,0)||!perf_frequency_)return;LARGE_INTEGER now{};QueryPerformanceCounter(&now);if(now.QuadPart<start)return;const LONG64 elapsed=now.QuadPart-start;auto& p=perf_[static_cast<size_t>(kind)];InterlockedIncrement64(&p.calls);InterlockedAdd64(&p.total,elapsed);
    LONG64 previous=InterlockedCompareExchange64(&p.maximum,0,0);while(previous<elapsed){const LONG64 actual=InterlockedCompareExchange64(&p.maximum,elapsed,previous);if(actual==previous)break;previous=actual;}}
struct PerfScope { PerfKind kind;LONGLONG start;explicit PerfScope(PerfKind value):kind(value),start(PerfStart()){}~PerfScope(){PerfEnd(kind,start);} };
void Logf(const char* format,...);
void PerfReport(){if(!InterlockedCompareExchange(&perf_enabled_,0,0)||!perf_frequency_||GetTickCount64()<perf_next_report_)return;perf_next_report_=GetTickCount64()+5000;
    const char* labels[]={"body_native_pre_plus_owned","head_native_pre_plus_filter","owned_body_cache","managed_grip_extra","forearm_poll","maintenance_250ms"};
    for(size_t i=0;i<perf_.size();++i){auto& p=perf_[i];const LONG64 calls=InterlockedExchange64(&p.calls,0),total=InterlockedExchange64(&p.total,0),maximum=InterlockedExchange64(&p.maximum,0);if(calls)Logf("NPC_PERF segment=%s calls=%lld avg_us=%.3f max_us=%.3f elapsed_qpc=1",labels[i],calls,static_cast<double>(total)*1e6/static_cast<double>(perf_frequency_)/static_cast<double>(calls),static_cast<double>(maximum)*1e6/static_cast<double>(perf_frequency_));}}

bool SetPerfEnabled(bool enabled){if(enabled){LARGE_INTEGER frequency{};if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return false;perf_frequency_=frequency.QuadPart;perf_next_report_=GetTickCount64()+5000;}
    InterlockedExchange(&perf_enabled_,0);for(auto& p:perf_){InterlockedExchange64(&p.calls,0);InterlockedExchange64(&p.total,0);InterlockedExchange64(&p.maximum,0);}InterlockedExchange(&perf_enabled_,enabled?1:0);
    Logf("NPC_PERF %s default_off=1 report_interval_ms=5000",enabled?"enabled":"disabled");return true;}

struct Identity { Object* object = nullptr; Name name;int32_t index=-1,serial=0; };
bool FaceReadable(const void* pointer,size_t bytes);
bool RawIdentity(Identity identity,int32_t index,int32_t serial);
struct Visibility { Identity component; bool visible = false, hidden = false; bool shadows_owned=false,cast_shadow=false,hidden_shadow=false,visible_written=false,hidden_written=false; };
#pragma warning(push)
#pragma warning(disable: 4324) // Native FTransform requires 16-byte parameter alignment.
struct SocketAdded {
    Identity owner, socket, skeleton; Name socket_name, bone_name; int32_t index = -1; Transform local;
    const GripBasis* grip = nullptr; Transform source_local; Name raw_bone_alias;
};
struct GripSocket {
    Name name, hand, raw_bone, palm; Object* socket=nullptr; Transform source_local, source_ref, target_ref, reference_local,source_palm_hand; double q0[4]{},hand_basis[4]{};
    int32_t bone=-1; // source bone of the same name: a route that is a bare bone (weapon_r, WEAPON_R, lowerarm_l) has no socket
    bool found=false; // added by RefreshWeaponRoutes for a weapon seen on that socket; dropped once no weapon hangs there
};
struct ForearmBinding {
    const ForearmBasis* basis=nullptr;Name bone;bool enabled=false;const char* reason="not-equipped";
    Identity actor,mesh;int32_t actor_index=-1,actor_serial=0,mesh_index=-1,mesh_serial=0;
    int32_t leader=-1,anim_class=-1,anim_instance=-1,hidden=-1;uint8_t hidden_byte=0,hidden_mask=0;
};
// proxy: a hand-chain bone this bone rests on (Hun's ik_weapon_r rests on weapon_r); mounts on it follow that hand.
struct GripAnchor { int32_t target=-1,palm=-1,target_palm=-1,proxy=-1;double basis[4]{0,0,0,1};bool hand=false,forearm=false; };
struct GripHook {
    Identity component; uintptr_t* original = nullptr; uintptr_t* table = nullptr;
    // Routes: the generated weapon mounts, plus any socket another actor's weapon (a switched or Shaqi
    // weapon) is found attached to at runtime (RefreshWeaponRoutes; never an effect, never the engine's
    // effect pools), dropped again once no weapon hangs on that socket.
    std::array<GripSocket,48> sockets{}; size_t count=0; volatile LONG queries=0, failures=0;
    std::array<ForearmBinding,2> forearms{};
    std::vector<GripAnchor> anchors;
    // Effect routes (BuildDisplayRoutes): a name the NPC display has and the hero mesh lacks (a boss's
    // BulletOrigin) reads the display's own socket; while an NPC skill runs (skill_route_until_) so do
    // the names both have, so the boss's effects and bullets start from the body that is drawn.
    // Sorted FName keys; the display is destroyed only after this hook is restored.
    Identity display; std::vector<uint64_t> display_only, shared; volatile LONG display_queries=0;
};
#pragma warning(pop)
struct SkeletonEdit { Identity skeleton; uintptr_t data = 0; bool old_flag = false; std::vector<uint8_t> original, written; };
struct FaceHook {
    Identity component, instance; Object* node = nullptr; int32_t instance_index = -1, instance_serial = 0;
    int32_t component_index = -1, component_serial = 0, runtime_bones = 0;
    uintptr_t* original = nullptr; std::unique_ptr<uintptr_t[]> table;
    const Profile* profile = nullptr; uint8_t old_curve = 0, old_attributes = 0;
    bool mesh_space=false; // fire weapon virtual bones have a different parent from their mapped source bone
    void* map_data = nullptr; int32_t map_slots = 0; uint64_t source_mesh = 0, target_mesh = 0;
    std::vector<int32_t> saved; volatile LONG failed = 0; uint32_t epochs = 0;
    bool map_written = false; const char* map_state = "unseen"; const char* restore_reason = nullptr; ULONGLONG restore_log_time = 0;
    bool body=false;uint64_t old_source_input=0,source_input=0,source_asset_input=0;
    std::vector<int32_t> body_saved;
};
std::vector<std::unique_ptr<FaceHook>> face_hooks_;
std::vector<std::unique_ptr<uintptr_t[]>> retired_face_tables_;
std::vector<Visibility> face_visibility_;
DWORD face_thread_ = 0;
ULONGLONG face_wait_started_ = 0, face_wait_seen_ = 0;
struct AnimOptions { Object* override_class = nullptr; int32_t lod = -1; bool disabled = false; };
AnimOptions primary_written_options_;
bool primary_options_written_ = false;
Identity pawn_, primary_, backup_, target_mesh_, original_class_;
Identity display_,source_asset_;
struct SourceSettings { bool visible=true,hidden=false,uro=false,cast_shadow=true,hidden_shadow=false;uint8_t tick_option=0;bool tick_enabled=false; };
SourceSettings source_settings_;
NpcSourceBudget::Lease source_budget_lease_;
void* source_budget_allocator_=nullptr;
struct PoseSession {
    Identity source,display,source_asset,target_asset;std::array<int32_t,6> generations{};NpcPoseTransfer::Plan plan;NpcPoseTransfer::Cache cache;
    // Per target bone: the source slot a virtual bone copies in the CopyPose map (its target bone's), else -1 (reference pose).
    std::vector<int32_t> virtual_source;
    Identity puppet; // the mount pose puppet (kMountLoops) once created, else empty
    volatile LONG failed=0;const char* why="not-evaluated";
};
std::unique_ptr<PoseSession> pose_session_;
SRWLOCK pose_lock_=SRWLOCK_INIT;
std::vector<Identity> added_;
std::vector<Visibility> hidden_;
std::vector<Visibility> authored_; // Created parts whose donor template is hidden.
ULONGLONG drift_log_time_=0;unsigned drift_logged_=0;
std::vector<SocketAdded> sockets_;
std::vector<Identity> socket_maps_;
SkeletonEdit skeleton_edit_;
SRWLOCK grip_lock_ = SRWLOCK_INIT;
std::unique_ptr<GripHook> grip_hook_;
// Like the engine Tick hook, retired vtable storage stays valid until process
// exit: a native caller may already have loaded the old table before restore.
std::vector<void*> retired_grip_tables_;
// Taller NPCs: the hero camera's pivot is raised by the NPC-minus-hero head
// height, through a named additive target offset on the ATPC camera that is
// set back to zero on restore. Small differences leave the camera alone.
// The full head difference: the actor itself never moves (the capsule keeps the hero half-height).
// The offset is set again every kCameraHoldMs so a camera mode change in a skill cannot drop it.
Identity camera_;double camera_lift_=0,camera_applied_=0;bool camera_owned_=false;ULONGLONG camera_probe_at_=0,camera_hold_at_=0;uint32_t camera_holds_=0;
constexpr ULONGLONG kCameraHoldMs=2000;
constexpr wchar_t kCameraAdditive[]=L"PBZ_NpcRetargetHeight";
// Giant bosses stand up to ~3 m above the hero's head (力者 +285 cm, 清道夫 +298 cm).
constexpr double kCameraLiftMin=5.0,kCameraLiftLow=-40.0,kCameraLiftHigh=300.0;
// Largest wrist turn MatchArmsToHero gives a hand, relative to its matched forearm.
constexpr double kWristMatchMaxDeg=45.0;
// Weapon size follows the body (NpcPoseTransfer::WeaponScale): 0.81x (WuWuWu) to 4.1x (PowerMan) on
// the 74 profiles; the clamp only guards a broken measurement.
constexpr double kWeaponScaleMin=0.5,kWeaponScaleMax=5.0;
// A hero bone resting this close to a hand-chain bone is an IK copy of it (NpcPoseTransfer::HandProxies).
constexpr double kHandProxyCm=0.5;
// Weapons attached to the pawn as last seen by RefreshWeaponRoutes (component, socket), logged once per change.
std::vector<std::pair<Object*,uint64_t>> weapon_children_;ULONGLONG weapon_log_due_=0;
// Body collision of the skin (NpcBodyCollision): the movement capsule takes the combat blueprint's
// own capsule (kNpcSkillSets[].monster), the other body shapes on the pawn scale with it. Everything
// written is recorded and put back on restore unless the game has since changed it.
constexpr double kCapsuleRatioMin=0.5,kCapsuleRatioMax=4.0;
struct ShapeEdit { Identity component;NpcBodyCollision::Shape original,applied;bool on_socket=false; };
// radius / half: the pawn class default capsule; kept_radius: the skin's (NpcBodyCollision::KeptRadius).
struct BodyCollision {
    bool owned=false;Identity pawn,capsule;double radius=0,half=0,kept_radius=0;
    NpcBodyCollision::CapsulePlan plan;std::vector<ShapeEdit> shapes;uint32_t reapplied=0;
};
BodyCollision body_;double stature_ratio_=0;
// Grabs keep the NPC display at its own size: GrabExtras scales the boss weapon by the skin's size over
// the grab's boss. skin_size_: the skin's rest pelvis height / the hero's.
double skin_size_=1;
// A montage authored for another skeleton on the hero (a boss skill, a borrowed grab) brings that
// skeleton's bone offsets: the display keeps its own rest translations meanwhile (NpcPoseTransfer::
// Evaluate foreign), eased over kForeignBlendMs. foreign_weight_ is read on the animation threads.
// foreign_own_: that montage is the NPC's own skeleton (its skills): the display takes the track back.
std::atomic<ULONGLONG> skill_route_until_{0};constexpr ULONGLONG kSkillRouteTailMs=2000; // effects a skill leaves behind
float audit_speed_=1.0f; // npcretarget audit speed <x> while an audit runs (world time dilation); 1 otherwise
std::atomic<float> skill_damage_{kSkillDamageDefault};constexpr wchar_t kSkillSection[]=L"NpcSkill"; // ScaleSkillDamage
std::atomic<double> foreign_weight_{0.0};std::atomic<bool> foreign_own_{false};double foreign_from_=0,foreign_to_=0;ULONGLONG foreign_start_=0;Object* foreign_montage_=nullptr;
constexpr ULONGLONG kForeignBlendMs=150;
// Pose puppet (kOwnLocomotion, kMountLoops): an invisible single-node SkeletalMeshComponent with the display mesh plays
// the boss's own animation: its own montage at the hero's position while one runs, else its idle / walk / run loop by
// the hero's speed and heading (NpcOwnLocomotion), positions advanced here (rate 0, no notifies). NpcPoseTransfer takes
// from it always the bones the hero has none of (玉妖's foot blades: at their reference pose they point 67 cm into the
// ground) and a mount (on a ridden one the rider's pelvis and legs too); a mount's whole body during own montages; and
// with the boss locomotion option (BOSS 自带移动动画) the whole body while the hero only moves (loco_body_, eased).
// mount_serial_ changes with the animation it plays (read on the animation path). The puppet is not in added_ (a pooled
// display keeps its owned components; the puppet goes with the look): RetireMount destroys it.
struct Mount { const MountLoops* row=nullptr;const OwnLocomotion* loco=nullptr;Identity puppet,display;
    Object* loops[NpcOwnLocomotion::kLoops]{};double length[NpcOwnLocomotion::kLoops]{};Object* playing=nullptr;
    int32_t gait=-1,dir=-1,loop=-1;double position=0,rate=1,body=0,speed=0;bool only_moves=false;
    ULONGLONG last_ms=0;uint32_t switches=0,body_logs=0;std::vector<Identity> pins;NpcOwnLocomotion::Travel travel; };
Mount mount_;std::atomic<uint32_t> mount_serial_{0};std::atomic<double> loco_body_{0.0};
// The look's height over the hero's (NpcHeroSize::LookRatio, 0 unknown) and the scale 主角体型 shows its display at now
// (read by the grip adapter on any thread).
double look_ratio_=0;std::atomic<double> hero_size_shown_{1.0};
// The boss locomotion option ([NpcRetarget] BossLocomotion, default off).
std::atomic<bool> boss_loco_{false};constexpr wchar_t kRetargetSection[]=L"NpcRetarget";
volatile LONG loco_probe_=0;SRWLOCK bone_probe_lock_=SRWLOCK_INIT;std::vector<std::string> bone_probe_; // tests: npcretarget loco status / boneprobe
// Display post-process nodes aimed by the boss's own state (QuietAimNodes): the instance last quieted.
Object* quiet_instance_=nullptr;
// A skin dropped because the map or the player changed is put back in the next scene: resume_entry_
// once the player has been valid for kResumeSettleMs (then the usual idle wait), at most kResumeTries.
int32_t resume_entry_=-1;ULONGLONG resume_at_=0;uint32_t resume_tries_=0;volatile LONG forget_resume_=0;
constexpr ULONGLONG kResumeSettleMs=2500;constexpr uint32_t kResumeTries=3;
// 过场动画: the look worn by the game's own cutscene copy of the hero (DriveCutsceneLook) instead of the player pawn;
// Retry by host generation, not a permanent blacklist for the whole cinematic.
bool cutscene_host_=false;
Identity cutscene_retry_actor_,cutscene_retry_mesh_,cutscene_retry_anim_;
NpcCutscene::Retry cutscene_retry_;
NpcCutscene::Playback cutscene_playback_;
// L3 + R3: the skin's boss skills (kNpcSkillSets[].skills) in random rounds (NpcBodyCollision::SkillRound),
// each granted to the hero's own ASC the way the game's own grab bullet grants one (K2_GiveAbility +
// TryActivateAbility) and removed when it ends. The combat blueprint and every cast skill class hold a
// counted strong reference while the skin is on, so the boss's montages, bullets and effects stay loaded.
// relock_until: R3 is also the game's lock-on toggle, so the lock is put back if lost until then.
// lock: the target struct the cast locked (RememberSkillLock), put back first when the lock is lost.
TrimChargeRuntime::Pause charge_pause_;
struct SkillCast { Identity asc,ability_class;int32_t handle=0;ULONGLONG started=0,relock_until=0;uint32_t relocks=0;std::string name;bool seen_active=false,owned=false,bare=false,lock_known=false;unsigned char lock[16]{}; };
SkillCast skill_;NpcBodyCollision::SkillRound skill_round_;std::vector<Identity> skill_pins_;volatile LONG skill_request_=0;
volatile LONG skill_request_pad_=0;  // the pad buttons held when the NPC skill key fired (RequestSkill): which chord fired it
// The NPC skill key's random pick: the curated first tier of a skin's moves (11-05 tables list every castable boss
// move after it, for the combo page and the audit).
size_t RandomCount(const NpcSkillSet& set){return set.random_count&&set.random_count<set.skill_count?set.random_count:set.skill_count;}
// TryActivateAbility says no while the hero is still finishing a move (no tag, cooldown or cost involved),
// so a refused skill stays granted and is tried again every kSkillRetryMs for kSkillWaitMs; only then does
// the press move on to the round's next skill (at most kSkillTriesPerPress skills per press).
// exact: a chosen skill (npcretarget cast, the audit); once it gives up waiting no other skill is tried.
// note: a chain step / dodge replacement names itself in the notice instead of the round count.
struct PendingCast { Identity asc,ability_class;int32_t handle=0;size_t pick=0,tries=0;ULONGLONG since=0,until=0,next_try=0,load_ms=0;std::string name;std::wstring note;bool pinned=false,exact=false,owned=false,bare=false; };  // owned: the hero's own spec; bare: a boss move granted to the hero without an NPC look (不变身)
PendingCast pending_;
// BOSS 技能 (NpcCombo::NodeType::BossSkill): the hero takes another look for moves of the chain. owner: the look the
// chain belongs to (-1: the hero's own); row: the look asked for. Going: waiting for that look, then the node's move
// plays; Wearing: its moves play in it; Returning: waiting for the owner's look again, then `node` (a move of the owner
// pressed meanwhile, -1 none) plays. The changes go through the waiting action like the page's own, behind the blink.
// target: the look Returning goes to (the owner's, or the look a 变身 / 变回主角 node or a 保留变身 BOSS 技能 set for
// the chain's end). home_before: combo_.home before this change asked to keep its look (back to it if the change fails).
struct Borrow { enum class Stage { Off, Going, Wearing, Returning }; Stage stage=Stage::Off; int32_t owner=-1,row=-1,target=-1,home_before=-2; int node=-1,button=0; ULONGLONG since=0,asked=0; uint32_t asks=0;
    std::string gate,note; };  // gate: what the look change waits on now (logged once each); note: what it went through (the dead flag)
Borrow borrow_;
bool blink_wanted_=false; // the queued look change is the player's or a chain's: it happens behind a blink
bool look_cancel_=false;  // the queued look change is a chain's (BOSS 技能 / back to the hero): the hero's action stops first
// 武器派生: the hero holding a weapon for a WeaponMove node (WeaponPresets' temporary weapon), behind a blink.
// Taking: asked, waiting for the weapon; Holding: in hand (the move plays, or the chain may play the weapon again);
// Returning: the weapon from before is asked back, then `node` (deferred) plays.
struct WeaponHold { enum class Stage { Off, Taking, Holding, Returning }; Stage stage=Stage::Off; int node=-1,button=0; std::string weapon; ULONGLONG since=0; };
WeaponHold weapon_;
constexpr ULONGLONG kWeaponTakeMs=8000,kWeaponReturnMs=8000,kWeaponNativeIdleMs=1200;
// The hero without an NPC look runs its own chain (NpcCombo::kHeroProfile) on its own pawn; its abilities are the
// hero's moves on the 招式派生 page.
Identity hero_pawn_;ULONGLONG hero_pawn_at_=0,hero_moves_at_=0;
SRWLOCK hero_moves_lock_=SRWLOCK_INIT;std::vector<std::string> hero_moves_;
constexpr ULONGLONG kSkillTimeoutMs=30000,kSkillWaitMs=2500,kSkillRetryMs=100,kSkillRelockMs=1500;constexpr size_t kMaxSkillPins=96,kSkillTriesPerPress=4; // 64 skills + boss weapons
constexpr uint32_t kSkillRelocksMax=3;
// While a skill runs the boss's own weapons (kNpcSkillSets[].weapons) hang on the NPC display and the
// hero's weapon actors are hidden part by part (HideHeroWeapons); both are undone when the skill ends.
// boss: the kNpcSkillSets[].weapons row the hero weapon stands for as a bullet / effect source.
// boss_weapon_rows_: the shown boss weapon component per weapons row (empty when it failed);
// skill_colliders_: the boss attack colliders copied for the skill (AddSkillColliders);
// seen_effects_: effects MoveSkillEffects has handled (parent: where it put or left it), or found before the skill.
struct HiddenPart { Identity component;bool was_hidden=false; };
struct HiddenWeapon { Identity actor;std::vector<HiddenPart> parts;size_t boss=0; };
struct SeenEffect { Identity effect;Object* parent=nullptr; };
std::vector<Identity> skill_weapons_,boss_weapon_rows_,skill_colliders_,skill_chains_;
// bare_weapons_: skill_weapons_ / hidden_weapons_ belong to a bare move (ShowBareWeapons: the boss weapons on the hero's
// own mesh, sized to the hero), not to a look: they do not make a hero chain see a look.
bool bare_weapons_=false;ULONGLONG bare_hide_at_=0;
// Skill support belongs to this cast, independently of whether a look is worn.
// Keep identities until stand-ins have been removed from their owner's weapon array.
Identity weapon_display_,weapon_pawn_,weapon_source_;
bool Same(Identity identity);
Object* WeaponDisplay(){return Same(weapon_display_)?weapon_display_.object:Same(display_)?display_.object:nullptr;}
Object* WeaponPawn(){return Same(weapon_pawn_)?weapon_pawn_.object:Same(pawn_)?pawn_.object:nullptr;}
Object* WeaponSource(){return Same(weapon_source_)?weapon_source_.object:Same(primary_)?primary_.object:nullptr;}
// The boss weapon rows of the running skin skill (SkinSkillWeapons, set by ShowSkillWeapons, cleared at its end): the
// boss Main weapon slot entry the move's GAA_SwitchWeapon actions hold (npc_weapon_sets.generated.h: 独眼's bow, 沐小葵's
// second sword ...), else the skin's kNpcSkillSets[].weapons. Their order is the display index the move's bullets and
// effect actions name. Rows() falls back to the skin's own list outside a skill.
struct WeaponRows { const NpcSkillWeapon* weapons=nullptr;size_t count=0;int32_t entry=-1; };
WeaponRows shown_weapons_;size_t skill_pick_=SIZE_MAX;size_t meshless_rows_=0;
// A move of another boss cast in a look (a BOSS 技能 with 不变身 while the hero wears an NPC look): weapon_skin_ is that
// boss's row (BareRow), whose weapons (and the move's weapon switch), colliders, stand-ins, trails, chains, body shapes
// and pillar the skill uses in place of the look's; weapon_scale_ sizes its weapons to the look's body
// (BareWeaponScale). -1: the look's own (selected_). Set at the cast (TryPendingSkill), cleared with the weapons.
int32_t weapon_skin_=-1;double weapon_scale_=1.0;
int32_t WeaponSkin(){return weapon_skin_>=0&&static_cast<size_t>(weapon_skin_)<std::size(kNpcSkillSets)?weapon_skin_:selected_;}
bool ForeignWeapons(){return bare_weapons_||(weapon_skin_>=0&&weapon_skin_!=selected_);}
WeaponRows Rows() {
    const int32_t skin=WeaponSkin();
    if(shown_weapons_.weapons||skin<0||static_cast<size_t>(skin)>=std::size(kNpcSkillSets))return shown_weapons_;
    const NpcSkillSet& set=kNpcSkillSets[skin];return WeaponRows{set.weapons,set.weapon_count,-1};
}
WeaponRows SkinSkillWeapons(size_t pick) {
    shown_weapons_={};WeaponRows rows=Rows();const int32_t skin=WeaponSkin();
    if(skin<0||static_cast<size_t>(skin)>=std::size(kNpcWeaponSetsBySkin))return rows;
    const NpcWeaponSets& sets=kNpcWeaponSetsBySkin[skin];
    if(pick>=sets.skill_count||!sets.start||sets.start[pick]<0||static_cast<size_t>(sets.start[pick])>=sets.entry_count)return rows;
    const NpcWeaponEntry& entry=sets.entries[sets.start[pick]];
    return entry.count&&entry.weapons?WeaponRows{entry.weapons,entry.count,sets.start[pick]}:rows;
}
struct VoiceSlot { Identity owner;Object** slot=nullptr;Object* written=nullptr;std::string original; };
std::vector<VoiceSlot> voice_slots_;std::vector<SeenEffect> seen_effects_;std::vector<HiddenWeapon> hidden_weapons_;size_t moved_effects_=0,unplaced_effects_=0;
// stand_ins_: the actors standing in for weapon display indices the hero's weapon lacks (AddStandIns), index = theirs.
struct StandIn { Identity actor;int32_t index=-1; };
std::vector<StandIn> stand_ins_;
// The boss weapon effect execs (ExecWeaponLookup / ExecTrailFind / ExecFaction) answer for the skill only while fx_live_.
// stand_in_host_: the hero weapon of display index 0, answered for a stand-in to the effect actions; fx_row_: the boss
// weapon row what spawns on it now belongs to (-1: its own). trail_configs_: the skin's boss weapon configs
// (kNpcTrailConfigSets, pinned in skill_pins_) for trail_configs_for_; trail_struct_: their Trails value struct.
std::atomic<bool> fx_live_{false};Identity stand_in_host_;int32_t fx_row_=-1;
std::vector<Identity> trail_configs_;int32_t trail_configs_for_=-1;Object* trail_struct_=nullptr;
size_t fx_redirects_=0,trails_lent_=0,trails_refused_=0,faction_forced_=0;
// Runtime audit (npcretarget audit start [skin] [skill] [end skin]): every skin from the start one is put on and
// each of its skills cast in order on a frozen hostile dummy kAuditDummyCm ahead of the hero's start spot (the
// hero under the boss-entry debug invulnerability, back on that spot before each cast). One NPC_AUDIT_SKILL
// line per skill: cast result, montage, boss weapons shown, colliders copied, new effect components by what
// they hang on, new bullet objects, health drops on the dummy. Damage is the boss's own (x1) meanwhile.
// offset_cm: on a hero part, how far that part's socket is from the same socket on the NPC body / boss weapon
// (-1: no NPC part has the socket, so the effect stays where the hero's hidden part is).
struct AuditFx { Identity effect;const char* anchor="world";std::string asset,socket;bool hidden=false;double offset_cm=-1; };
struct AuditState {
    enum class Phase { Off, Begin, Dummy, DummyWait, Skin, Settle, Run, GrabNext, GrabPrepare, GrabTrigger, GrabRun, Finish } phase=Phase::Off;
    size_t profile=0,skill=0,first_skill=0,end=0,skins=0,skills=0;bool requested=false,placed=false,started=false;
    // grabs mode (npcretarget audit grabs [first variant]): every supported grab variant on the live dummy.
    bool grabs=false,grab_active=false,grab_paired=false;int grab_turns=0;double grab_yaw0=0;size_t grab=0,grabs_done=0;int32_t grab_index=-1;uint32_t grab_attempts=0,grab_activations=0,grab_hits=0;
    MotianGrab::Options grab_options;float grab_health=0;double grab_distance=150;
    ULONGLONG at=0,deadline=0,next_scan=0,begun=0,cast_at=0;double scan_ms=0;
    Identity dummy;Vector home{};double home_yaw=0;std::string stop;
    // A skill whose bullets missed the dummy at kAuditDummyCm is cast once more with it at kAuditCloseCm:
    // a hit there is reach / warping, none at all a missing attack collider.
    bool close_retry=false,hero_weapons_dumped=false;double dummy_cm=0;
    std::vector<size_t> only; // npcretarget audit list: just these skills (0-based, ascending) of the one skin
    std::string result,montage;size_t weapons_shown=0,weapons_expected=0,colliders=0,effects_moved=0,stand_ins=0;
    uint32_t hits=0,dummy_hits=0,targets=0,weapon_swaps=0;float native=0,dummy_damage=0;
    std::vector<Identity> fx_base,bullet_base;std::vector<AuditFx> fx;std::vector<std::pair<Identity,std::string>> bullets;
    std::vector<std::string> weapon_gaps; // OriginType/WeaponIndex of bullets that got no hero weapon actors
    std::vector<Object*> collision_dumped; // bullet classes whose target / collision the close retry logged
};
AuditState audit_;
// 1 start, 2 stop, 3 grabs, 4 export (request_lock). only: audit list's 0-based skills of skin `from`, ascending.
struct AuditRequest { int32_t action=0;size_t from=0,skill=0,end=0;std::vector<size_t> only; };
AuditRequest audit_request_;volatile LONG cast_request_=0; // npcretarget cast <n>: n + 1
// Tests without the game's input (npcretarget bare <skin> <skill> | press <bits hex> | resources): a 不变身 move as the
// chain casts it, a hero-action press into the chain, the look-resource state; handled on the game thread (Tick).
volatile LONG bare_test_=0,press_test_=0,press_test_hold_=0,resource_probe_=0;
bool Auditing() { return audit_.phase!=AuditState::Phase::Off; }

Object* Class(const char* path) { return ue::FindObject(path); }
bool Same(Identity identity) { return RawIdentity(identity,identity.index,identity.serial)&&ue::Alive(identity.object); }
Identity Id(Object* object) {
    Identity result;result.object=object;if(!object||!ue::Base()||!FaceReadable(object,0x28))return result;result.name=ue::NameOf(object);result.index=ue::At<int32_t>(object,ue::layout::kObjIndex);
    if(result.index<0||result.index>=ue::detail::ObjectCount()||ue::detail::ObjectAt(result.index)!=object)return result;
    auto* array=reinterpret_cast<Object*>(ue::Base()+ue::layout::kObjectArray);Object** chunks=ue::At<Object**>(array,24);const uint32_t chunk=static_cast<uint32_t>(result.index)>>16;
    if(!FaceReadable(chunks,(static_cast<size_t>(chunk)+1)*8)||!chunks[chunk])return result;Object* item=chunks[chunk]+(static_cast<uint32_t>(result.index)&65535)*24ull;
    if(FaceReadable(item,24))result.serial=ue::At<int32_t>(item,16);
    // Newly deferred components may not yet have any weak references. Let the
    // native FWeakObjectPtr constructor allocate its serial; never invent one.
    if(result.serial<=0){static constexpr unsigned char weak[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x12,0x48,0x8b,0xd9,0x48,0x85,0xd2,0x74};void* native=reinterpret_cast<void*>(ue::Base()+0x166e900);
        if(FaceReadable(native,sizeof(weak))&&!std::memcmp(native,weak,sizeof(weak))){int32_t value[2]{};reinterpret_cast<void(__fastcall*)(int32_t*,Object* const*)>(native)(value,&object);if(value[0]==result.index)result.serial=value[1];}}
    return result;
}
// The look's resources. The hero's own chain move (an owned skill: the hero's own ability, 武器派生, 瞬移; or a bare one:
// a 不变身 boss move and its boss weapons) is none of them: it must not make a hero chain see a look (its owner) and
// reset itself (a bare move did until 11-08: the chain reset the moment it played, and never started again).
bool HasResources() {
    return pose_session_ || display_.object || primary_.object || backup_.object || !added_.empty() || !hidden_.empty() || !sockets_.empty() || skeleton_edit_.skeleton.object || !face_hooks_.empty() || grip_hook_ ||
        body_.owned || (skill_.handle > 0 && !skill_.owned && !skill_.bare) || (pending_.handle > 0 && !pending_.owned && !pending_.bare) || !skill_pins_.empty() ||
        NpcSkillSupport::LookResources(bare_weapons_,skill_weapons_.size(),hidden_weapons_.size(),skill_colliders_.size(),stand_ins_.size(),skill_chains_.size()) || !voice_slots_.empty();
}
std::string Text(Name name) { char text[192]{}; ue::NameText(name, text, sizeof(text)); return text; }
std::string Text(Object* object) { return object ? Text(ue::NameOf(object)) : "-"; }
std::string Lower(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}
void Logf(const char* format, ...) {
    if (!logger_) return;
    char line[1000]{}; va_list arguments; va_start(arguments, format); _vsnprintf_s(line, sizeof(line), _TRUNCATE, format, arguments); va_end(arguments); logger_(line);
}
void Status(const wchar_t* format, ...) {
    wchar_t text[640]{}; va_list arguments; va_start(arguments, format); _vsnwprintf_s(text, _countof(text), _TRUNCATE, format, arguments); va_end(arguments);
    AcquireSRWLockExclusive(&snapshot_lock); snapshot_.status = text; ReleaseSRWLockExclusive(&snapshot_lock);
}
std::string Utf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1) return {};
    std::string value(static_cast<size_t>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, text, -1, value.data(), count, nullptr, nullptr)) return {};
    value.pop_back(); return value;
}
bool Fail(const wchar_t* message) { failure_text_ = message; Status(L"NPC 重定向已停止：%s", message); Logf("NPC_RETARGET refused at %s: %s", stage_, Utf8(message).c_str()); return false; }
bool NativeHelpersMatch() {
    if (!ue::Base()) return false;
    for (const NativeSignature& signature : kNativeSignatures)
        if (std::memcmp(reinterpret_cast<const void*>(ue::Base() + signature.rva), signature.code, sizeof(signature.code))) return false;
    return true;
}
int32_t published_chain_ = -3;  // game thread: snapshot_.chain as last published
void Publish(bool busy = false) {
    AcquireSRWLockExclusive(&snapshot_lock);
    snapshot_.active = active_; snapshot_.busy = busy; snapshot_.parts = static_cast<int32_t>(added_.size());
    snapshot_.cleanup_pending = cleanup_pending_; snapshot_.disabled = InterlockedCompareExchange(&disabled_, 0, 0) != 0;
    snapshot_.appearance_gate = release_appearance_ != nullptr;
    snapshot_.experimental = InterlockedCompareExchange(&experimental_, 0, 0) != 0;
    snapshot_.sockets = grip_hook_?static_cast<int32_t>(grip_hook_->count):0; snapshot_.retargeted_bones = active_ && selected_ >= 0 ? static_cast<int32_t>(source_driver_mode_&&pose_session_?pose_session_->plan.mapped:kProfiles[selected_].shared_count) : 0;
    snapshot_.selected = active_ && selected_ >= 0 ? kProfiles[selected_].name : L"";
    snapshot_.skin = active_ ? selected_ : -1;
    snapshot_.chain = borrow_.stage != Borrow::Stage::Off ? borrow_.owner : snapshot_.skin;
    published_chain_ = snapshot_.chain;
    ReleaseSRWLockExclusive(&snapshot_lock);
}
// Whose chain the page edits follows the borrow too: a BOSS 技能 / 变身 look ending (borrow_ back to Off) changes it
// with no look change of its own. A 保留变身 / 变身 look the chain ended in is then the skin, and its own chain is
// the one that runs (ComboReady) and the one the page shows; the page kept the hero's until the next look change.
void PublishChain() {
    const int32_t chain = borrow_.stage != Borrow::Stage::Off ? borrow_.owner : active_ ? selected_ : -1;
    if (chain == published_chain_) return;
    AcquireSRWLockExclusive(&snapshot_lock);
    snapshot_.chain = chain; published_chain_ = chain;
    ReleaseSRWLockExclusive(&snapshot_lock);
}
uintptr_t Field(Object* strukt, const char* property) {
    for (Object* type = strukt; type; type = ue::At<Object*>(type, ue::layout::kSuper)) {
        uintptr_t field = ue::At<uintptr_t>(type, ue::layout::kChildProperties);
        for (size_t guard = 0; field && guard < 4096; ++guard) {
            if (ue::NameIs(ue::At<Name>(reinterpret_cast<void*>(field), ue::layout::kPropName), property)) return field;
            field = ue::At<uintptr_t>(reinterpret_cast<void*>(field), ue::layout::kPropNext);
        }
    }
    return 0;
}
int32_t Offset(Object* object, const char* property) { return object ? ue::Offset(ue::ClassOf(object), property) : -1; }
uint64_t FieldKind(Object* property);
template<class T> T Property(Object* object, const char* property) {
    const int32_t offset = Offset(object, property);
    return offset >= 0 ? ue::At<T>(object, offset) : T{};
}
template<class T> bool Put(Object* object, const char* property, const T& value) {
    const uintptr_t field = object ? Field(ue::ClassOf(object), property) : 0;
    if (!field || ue::At<int32_t>(reinterpret_cast<void*>(field), ue::layout::kPropElementSize) != static_cast<int32_t>(sizeof(T))) return false;
    const int32_t offset = ue::At<int32_t>(reinterpret_cast<void*>(field), ue::layout::kPropOffset);
    std::memcpy(object + offset, &value, sizeof(value)); return true;
}
bool Bool(Object* object, const char* property, bool& value) {
    const uintptr_t field = object ? Field(ue::ClassOf(object), property) : 0;
    if (!field || ue::At<uint8_t>(reinterpret_cast<void*>(field), 0x70) != 1) return false;
    const int32_t offset = ue::At<int32_t>(reinterpret_cast<void*>(field), ue::layout::kPropOffset);
    const uint8_t byte_offset = ue::At<uint8_t>(reinterpret_cast<void*>(field), 0x71), mask = ue::At<uint8_t>(reinterpret_cast<void*>(field), 0x73);
    if (byte_offset > 7 || !mask) return false;
    value = (ue::At<uint8_t>(object, offset + byte_offset) & mask) != 0; return true;
}
bool PutBool(Object* object, const char* property, bool value) {
    const uintptr_t field = object ? Field(ue::ClassOf(object), property) : 0;
    if (!field || ue::At<uint8_t>(reinterpret_cast<void*>(field), 0x70) != 1) return false;
    const int32_t offset = ue::At<int32_t>(reinterpret_cast<void*>(field), ue::layout::kPropOffset);
    const uint8_t byte_offset = ue::At<uint8_t>(reinterpret_cast<void*>(field), 0x71), byte_mask = ue::At<uint8_t>(reinterpret_cast<void*>(field), 0x72), mask = ue::At<uint8_t>(reinterpret_cast<void*>(field), 0x73);
    if (byte_offset > 7 || !mask || !byte_mask) return false;
    uint8_t& byte = ue::At<uint8_t>(object, offset + byte_offset);
    byte = static_cast<uint8_t>((byte & ~mask) | (value ? byte_mask : 0)); return true;
}
bool VisibilityOf(Object* object, bool& visible, bool& hidden) { return Bool(object, "bVisible", visible) && Bool(object, "bHiddenInGame", hidden); }
bool SetVisible(Object* object, bool visible, bool hidden) {
    Call show(object, "SetVisibility"), hide(object, "SetHiddenInGame");
    return show.Set("bNewVisibility", visible).Set("bPropagateToChildren", false).Invoke() &&
        hide.Set("NewHidden", hidden).Set("bPropagateToChildren", false).Invoke();
}
bool SetVisibleOwned(Object* object,bool visible,bool hidden,bool& visible_written,bool& hidden_written) {
    visible_written=true;if(!Call(object,"SetVisibility").Set("bNewVisibility",visible).Set("bPropagateToChildren",false).Invoke())return false;
    hidden_written=true;return Call(object,"SetHiddenInGame").Set("NewHidden",hidden).Set("bPropagateToChildren",false).Invoke();
}
bool RestoreVisibleOwned(Object* object,bool original_visible,bool original_hidden,bool& visible_written,bool& hidden_written) {
    bool visible=false,hidden=false;if(!VisibilityOf(object,visible,hidden))return false;
    if(visible_written){if(!visible&&!Call(object,"SetVisibility").Set("bNewVisibility",original_visible).Set("bPropagateToChildren",false).Invoke())return false;visible_written=false;}
    if(hidden_written){if(hidden&&!Call(object,"SetHiddenInGame").Set("NewHidden",original_hidden).Set("bPropagateToChildren",false).Invoke())return false;hidden_written=false;}return true;
}
Object* Kismet() { return ue::Default("/Script/Engine.Default__KismetSystemLibrary"); }
Object* Math() { return ue::Default("/Script/Engine.Default__KismetMathLibrary"); }
Object* SkelMesh(Object* component) {
    Call get(component, "GetSkeletalMeshAsset");
    return get.Invoke() ? get.Get<Object*>("ReturnValue") : nullptr;
}
Object* AnimClass(Object* component) { Call get(component, "GetAnimClass"); return get.Invoke() ? get.Get<Object*>("ReturnValue") : nullptr; }
Object* AnimInstance(Object* component) { Call get(component, "GetAnimInstance"); return get.Invoke() ? get.Get<Object*>("ReturnValue") : nullptr; }
bool SetMesh(Object* component, Object* mesh) {
    return Call(component, "SetSkeletalMeshAsset").Set("NewMesh", mesh).Invoke() && SkelMesh(component) == mesh;
}
bool SetPhysics(Object* component, Object* physics) {
    return Call(component, "SetPhysicsAsset").Set("NewPhysicsAsset", physics).Set("bForceReInit", true).Invoke();
}
bool ReadAnimOptions(Object* component, AnimOptions& options) {
    if (Offset(component, "OverridePostProcessAnimBP") < 0 || Offset(component, "PostProcessAnimBPLODThreshold") < 0 ||
        !Bool(component, "bDisablePostProcessBlueprint", options.disabled)) return false;
    options.override_class = Property<Object*>(component, "OverridePostProcessAnimBP");
    options.lod = Property<int32_t>(component, "PostProcessAnimBPLODThreshold");
    return !options.override_class || ue::Alive(options.override_class);
}
bool SameAnimOptions(const AnimOptions& a, const AnimOptions& b) {
    return a.override_class == b.override_class && a.lod == b.lod && a.disabled == b.disabled;
}
bool OwnedAnimOptions(const AnimOptions& current, const AnimOptions& original, const AnimOptions& written) {
    // A failed native setter may have completed only part of the write. Permit
    // our original/written values independently, but preserve foreign values.
    return (current.override_class == original.override_class || current.override_class == written.override_class) &&
        (current.lod == original.lod || current.lod == written.lod) &&
        (current.disabled == original.disabled || current.disabled == written.disabled);
}
bool WriteAnimOptions(Object* component, const AnimOptions& options) {
    // Native setters own animation-instance lifecycle. Do not copy transient
    // AnimInstance pointers or a template's node/proxy/DNA buffers.
    return Call(component, "SetOverridePostProcessAnimBP").Set("InPostProcessAnimBlueprint", options.override_class)
        .Set("ReinitAnimInstances", false).Invoke() &&
        Call(component, "SetDisablePostProcessBlueprint").Set("bInDisablePostProcess", options.disabled).Invoke() &&
        Put(component, "PostProcessAnimBPLODThreshold", options.lod);
}
bool ReinitializeAnimation(Object* component) {
    return Call(component, "SetAnimationMode").Set("InAnimationMode", uint8_t{0}).Set("bForceInitAnimScriptInstance", true).Invoke();
}
Object* EffectivePostClass(Object* component) {
    Object* override_class = Property<Object*>(component, "OverridePostProcessAnimBP");
    return override_class ? override_class : Property<Object*>(SkelMesh(component), "PostProcessAnimBlueprint");
}
bool VerifyPostProcess(Object* component, const AnimOptions& expected, const char* role) {
    AnimOptions actual;
    if (!ReadAnimOptions(component, actual) || !SameAnimOptions(actual, expected)) return false;
    Object* expected_class = EffectivePostClass(component), *instance = Property<Object*>(component, "PostProcessAnimInstance");
    // Disabled components can still hold an initialized instance, but a live
    // instance may never retain the previous asset's post-process class.
    const bool matches = expected_class ? (ue::Alive(instance) && ue::ClassOf(instance) == expected_class) : instance == nullptr;
    Logf("NPC_RETARGET_POST role=%s component=%s mesh=%s override=%s expected=%s instance=%s actualClass=%s disabled=%d lod=%d verified=%d",
        role, Text(component).c_str(), Text(SkelMesh(component)).c_str(), Text(actual.override_class).c_str(), Text(expected_class).c_str(),
        Text(instance).c_str(), Text(instance ? ue::ClassOf(instance) : nullptr).c_str(), actual.disabled ? 1 : 0, actual.lod, matches ? 1 : 0);
    return matches;
}
bool HasNoLeader(Object* component) {
    const int32_t offset = Offset(component, "LeaderPoseComponent");
    if (offset < 0) return false;
    // Reflected FWeakObjectPtr: null may use index 0 or INDEX_NONE; no serial
    // means it cannot identify a live leader. Never resolve raw UObject refs.
    const int32_t index = ue::At<int32_t>(component, offset), serial = ue::At<int32_t>(component, offset + 4);
    return serial == 0 && (index == 0 || index == -1);
}
bool AnimationApiReady(Object* component) {
    Call override_post(component, "SetOverridePostProcessAnimBP"), disable(component, "SetDisablePostProcessBlueprint"),
        mode(component, "SetAnimationMode"), prerequisite(component, "AddTickPrerequisiteComponent");
    return override_post.Slot("InPostProcessAnimBlueprint", sizeof(Object*)) && override_post.Slot("ReinitAnimInstances", sizeof(bool)) &&
        disable.Slot("bInDisablePostProcess", sizeof(bool)) && mode.Slot("InAnimationMode", sizeof(uint8_t)) &&
        mode.Slot("bForceInitAnimScriptInstance", sizeof(bool)) && prerequisite.Slot("PrerequisiteComponent", sizeof(Object*));
}
bool TransformOf(Object* component, Transform& result) {
    Call get(component, "GetRelativeTransform"); if (!get.Slot("ReturnValue", sizeof(result)) || !get.Invoke()) return false;
    result = get.Get<Transform>("ReturnValue"); return true;
}
bool Compose(const Transform& a, const Transform& b, Transform& result) {
    Call compose(Math(), "ComposeTransforms");
    if (!compose.Set("A", a).Set("B", b).Invoke()) return false;
    result = compose.Get<Transform>("ReturnValue"); return true;
}
bool Relative(const Transform& a, const Transform& b, Transform& result) {
    Call relative(Math(), "MakeRelativeTransform");
    if (!relative.Set("A", a).Set("RelativeTo", b).Invoke()) return false;
    result = relative.Get<Transform>("ReturnValue"); return true;
}
bool TransformNear(const Transform& expected, const Transform& actual) {
    double dot = 0;
    for (size_t i = 0; i < 4; ++i) dot += expected.rotation[i] * actual.rotation[i];
    if (std::abs(std::abs(dot) - 1) > 1e-5) return false;
    for (size_t i = 0; i < 3; ++i)
        if (std::abs(expected.translation[i] - actual.translation[i]) > 1e-7 || std::abs(expected.scale[i] - actual.scale[i]) > 1e-7) return false;
    return true;
}
std::vector<Object*> Components(Object* actor, Object* cls) {
    Call get(actor, "K2_GetComponentsByClass"); std::vector<Object*> result;
    if (!get.Set("ComponentClass", cls).Invoke()) return result;
    const Array array = get.Get<Array>("ReturnValue");
    if (array.num >= 0 && array.num < 2048 && (array.num == 0 || array.data))
        for (int32_t i = 0; i < array.num; ++i) if (ue::Alive(static_cast<Object**>(array.data)[i])) result.push_back(static_cast<Object**>(array.data)[i]);
    ue::Free(array.data); return result;
}
struct Context { Object* world = nullptr; Object* pawn = nullptr; Object* mesh = nullptr; };
Object* ObjectWorld(Object* object) {
    Object* world_class = Class("/Script/Engine.World"), *level_class = Class("/Script/Engine.Level");
    for (size_t i = 0; ue::Alive(object) && i < 32; ++i, object = ue::OuterOf(object)) {
        if (ue::IsA(object, world_class)) return object;
        if (ue::IsA(object, level_class)) return Property<Object*>(object, "OwningWorld");
    }
    return nullptr;
}
Object* ForeignSkeletonUser(const Context& context, Object* skeleton) {
    Object* conflict = nullptr;
    Object* skinned_class = Class("/Script/Engine.SkinnedMeshComponent"), *skeletal_class = Class("/Script/Engine.SkeletalMeshComponent");
    ue::ForEachObject([&](Object* component) {
        if (ue::IsTemplate(component) || !ue::Alive(component) || !ue::IsA(component, skinned_class)) return true;
        Object* asset = ue::IsA(component, skeletal_class) ? SkelMesh(component) : Property<Object*>(component, "SkinnedAsset");
        if (!asset || Property<Object*>(asset, "Skeleton") != skeleton) return true;
        Call get_owner(component, "GetOwner");
        Object* owner = get_owner.Invoke() ? get_owner.Get<Object*>("ReturnValue") : nullptr;
        // Include hidden/unregistered live components conservatively: they may
        // become active without another Apply. Never mistake the unregistered
        // original backup or our new followers for a foreign actor.
        if (owner == context.pawn || component == context.mesh || component == backup_.object ||
            std::any_of(added_.begin(), added_.end(), [&](Identity id) { return Same(id) && id.object == component; })) return true;
        if (ObjectWorld(owner ? owner : component) != context.world) return true;
        conflict = component; return false;
    });
    return conflict;
}
bool ContextNow(Context& context) {
    if (!ue::Alive(engine_)) engine_ = ue::FindEngine();
    Object* viewport = Property<Object*>(engine_, "GameViewport");
    context.world = Property<Object*>(viewport, "World");
    if (!ue::Alive(context.world)) return false;
    Object* instance = Property<Object*>(context.world, "OwningGameInstance");
    const Array players = Property<Array>(instance, "LocalPlayers");
    if (players.num < 1 || players.num > 16 || !players.data) return false;
    Object* local = static_cast<Object**>(players.data)[0], *controller = Property<Object*>(local, "PlayerController");
    context.pawn = Property<Object*>(controller, "Pawn"); context.mesh = Property<Object*>(context.pawn, "Mesh");
    return ue::Alive(context.pawn) && ue::Alive(context.mesh) && SkelMesh(context.mesh) && AnimClass(context.mesh);
}
// 身材 / 骨骼 (the UUEPBS editor, NpcBridge; 2026-11-13 in place of the 骨骼变换 page): the hero's own body only. Its
// skeleton is handed to the editor and the sliders are written right after the pose finalize, through BoneMotionRuntime's
// hook (one hook per component, shared with the 骨骼变换 node: NpcBody::Hook). While a look is on, or its request waits,
// the hero's body is the game's (its pose drives the look): no listener. [NpcBody] Enabled (default 0) gates all of it;
// [NpcBody] Hook=0 hands the skeleton over but never writes.
struct BodyVec3 { double x = 0.0, y = 0.0, z = 0.0; };
bool body_settings_read_ = false, body_hook_enabled_ = true, body_look_mode_ = false, body_fed_logged_ = false;
void* body_last_mesh_ = nullptr;
// The hero's speed (cm/s) for the editor's locomotion layers (idle / walk / run / sprint by 30 / 300 / 700, as UUEPBS's
// config.lua; its Lua sent CharacterMovement.Velocity every 150 ms).
double BodySpeed(Object* pawn) {
    if (!pawn) return 0.0;
    ue::Call get(pawn, "GetVelocity");
    if (!get.Invoke()) return 0.0;
    const BodyVec3 v = get.Get<BodyVec3>("ReturnValue");
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
void DriveBodyImpl() {
    static Context context;
    if (!NpcBridge::Enabled()) {
        if (NpcBody::HookedComponent()) { NpcBody::Unhook(); Logf("NPC_BODY off ([NpcBody] Enabled=0): the hero's body is the game's"); }
        return;
    }
    // A look (or its request): the hero's body goes back to the game at once, before ContextNow can refuse this frame.
    if (source_driver_mode_ || active_ || waiting_action_ >= 0) {
        if (NpcBody::HookedComponent()) { NpcBody::Unhook(); Logf("NPC_BODY paused: a look is on, the hero's body is the game's until it returns"); }
        body_look_mode_ = true;
        return;
    }
    if (!ContextNow(context)) return;
    if (body_look_mode_) { body_look_mode_ = false; Logf("NPC_BODY back on the hero"); }
    if (!body_settings_read_) {
        body_settings_read_ = true;
        body_hook_enabled_ = SettingsStore::GetInt(L"NpcBody", L"Hook", 1) != 0;
        Logf("NPC_BODY [NpcBody] Hook=%d (0: the skeleton is handed over, the sliders are never written)", body_hook_enabled_ ? 1 : 0);
    }
    if (!NpcBody::Tick(context.mesh)) return;
    NpcBridge::SetLocoSpeed(BodySpeed(context.pawn));
    const bool fed = NpcBridge::Feed(context.mesh);
    if (fed && !body_fed_logged_) { body_fed_logged_ = true; Logf("NPC_BODY skeleton handed to the editor: bones=%d", NpcBody::SnapshotCount()); }
    // Started hidden: it applies the last session's sliders; F11 or the page's button shows it.
    if (fed && !NpcBridge::Started()) { NpcBridge::Start(false); Logf("NPC_BODY editor %s (hidden until opened)", NpcBridge::Started() ? "started" : "failed to start"); }
    if (context.mesh != body_last_mesh_) { Logf("NPC_BODY hero mesh %p -> %p", body_last_mesh_, context.mesh); body_last_mesh_ = context.mesh; }
    if (fed && body_hook_enabled_ && NpcBody::HookedComponent() != context.mesh) {
        NpcBody::Hook(context.mesh);
        Logf("NPC_BODY pose listener requested on %p (BoneMotionRuntime's hook, shared with the 骨骼变换 node)", context.mesh);
    }
}
void DriveBody() {
    __try { DriveBodyImpl(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        static LONG quiet = 0;
        if (InterlockedIncrement(&quiet) <= 8) Logf("NPC_BODY fault code=0x%08lx", static_cast<unsigned long>(GetExceptionCode()));
    }
}
// What the hero is doing: a montage playing, its speed (cm/s) and its movement mode.
struct HeroMotion { bool known = false, montage = false; double speed = 0; uint8_t mode = 0; };
HeroMotion MotionOf(const Context& context) {
    HeroMotion motion; Object* instance = AnimInstance(context.mesh);
    if (!instance) return motion;
    Call montage(instance, "IsAnyMontagePlaying");
    if (!montage.Slot("ReturnValue", sizeof(bool)) || !montage.Invoke()) return motion;
    motion.montage = montage.Get<bool>("ReturnValue");
    Call velocity(context.pawn, "GetVelocity");
    if (!velocity.Invoke()) return motion;
    const Vector v = velocity.Get<Vector>("ReturnValue");
    if (!std::isfinite(v.x + v.y + v.z)) return motion;
    motion.speed = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    motion.mode = Property<uint8_t>(Property<Object*>(context.pawn, "CharacterMovement"), "MovementMode");
    motion.known = true; return motion;
}
std::string MotionText(const HeroMotion& motion) {
    static const char* const kModes[] = {"none","walking","navwalking","falling","swimming","flying","custom"};
    char text[96]{}; _snprintf_s(text, sizeof(text), _TRUNCATE, "montage=%d speed=%.0f mode=%s", motion.montage ? 1 : 0, motion.speed,
                                 motion.mode < std::size(kModes) ? kModes[motion.mode] : "?");
    return motion.known ? text : "unknown";
}
// No cutscene and the hero shown: all a look change needs. The NPC look is a display mesh that copies the hero's
// pose (source driver mode); the hero's own mesh, anim instance and movement are not touched, so a look goes on or
// off in any action (attack, dodge, hit, air, run) without waiting for it to end.
// A LevelSequence (cutscene) of the player's world is playing. GetAllActorsOfClass lists the world's actors in every
// loaded level: one in the persistent level counts as soon as it plays; one in a streamed sublevel (its outer is that
// level's own UWorld, so it was never found here before 11-08: the look never went onto the cutscene copy) counts while
// the player is in a cinematic (the controller's bCinematicMode, or the hero hidden), so a looping ambient sequence of a
// sublevel never blocks looks. The first sublevel cutscene of each sequence actor is logged.
Object* logged_sequence_=nullptr;
bool SequencePlaying(const Context& context) {
    // The game's cinematic manager excludes looping ambient sequences and
    // stays active across shot/subsequence changes. Actor scanning is only a
    // compatibility fallback when the manager cannot be queried.
    Call subsystem(ue::Default("/Script/Engine.Default__SubsystemBlueprintLibrary"),"GetWorldSubsystem");
    Object* manager_class=Class("/Script/ProjectPBZ.PBZLevelSequenceManager");
    Object* manager=subsystem.Set("ContextObject",context.world).Set("Class",manager_class).Invoke()?subsystem.Get<Object*>("ReturnValue"):nullptr;
    if(!manager){Call game(ue::Default("/Script/Engine.Default__SubsystemBlueprintLibrary"),"GetGameInstanceSubsystem");
        manager=game.Set("ContextObject",context.world).Set("Class",manager_class).Invoke()?game.Get<Object*>("ReturnValue"):nullptr;}
    Call state(manager,"IsInSequence");
    if(manager&&state.Slot("ReturnValue",sizeof(bool))&&state.Invoke())return state.Get<bool>("ReturnValue");
    bool persistent = false, streamed = false; Object* streamed_actor = nullptr;
    Object* sequence_class = Class("/Script/LevelSequence.LevelSequenceActor");
    const auto playing = [&](Object* actor) {
        Object* player = Property<Object*>(actor, "SequencePlayer");
        if (!ue::Alive(player)) return false;
        Call now(player, "IsPlaying");
        return now.Invoke() && now.Get<bool>("ReturnValue");
    };
    const auto in_world = [&](Object* actor) {
        Object* outer = ue::OuterOf(actor);
        for (size_t i = 0; outer && i < 8; ++i, outer = ue::OuterOf(outer)) if (outer == context.world) return true;
        return false;
    };
    const auto look_at = [&](Object* actor, bool listed) {
        if (ue::IsTemplate(actor) || !ue::Alive(actor) || !ue::IsA(actor, sequence_class)) return;
        const bool own = in_world(actor);
        if (!own && !listed) return;  // the fallback walk sees every world's objects: only the player's own then
        if (!playing(actor)) return;
        if (own) persistent = true; else { streamed = true; streamed_actor = actor; }
    };
    // The world's sequence actors (GameplayStatics.GetAllActorsOfClass walks the world's actors, not every object).
    Call actors(ue::Default("/Script/Engine.Default__GameplayStatics"), "GetAllActorsOfClass");
    if (sequence_class && actors.Set("WorldContextObject", context.pawn ? context.pawn : context.world).Set("ActorClass", sequence_class).Invoke()) {
        const Array list = actors.Get<Array>("OutActors");
        if (list.data && list.num > 0 && list.num <= 4096 && FaceReadable(list.data, static_cast<size_t>(list.num) * sizeof(Object*)))
            for (int32_t i = 0; i < list.num && !persistent; ++i) look_at(static_cast<Object**>(list.data)[i], true);
        ue::Free(list.data);
    } else if (sequence_class) ue::ForEachObject([&](Object* actor) { look_at(actor, false); return !persistent; });
    if (persistent) return true;
    if (!streamed) return false;
    bool cinematic = false, hidden = false;
    if (Object* controller = context.pawn ? Property<Object*>(context.pawn, "Controller") : nullptr) Bool(controller, "bCinematicMode", cinematic);
    if (context.pawn) Bool(context.pawn, "bHidden", hidden);
    if ((cinematic || hidden) && streamed_actor != logged_sequence_) {
        logged_sequence_ = streamed_actor;
        Logf("NPC_RETARGET cutscene in a streamed level: %s (%s) plays, cinematicMode=%d heroHidden=%d", Text(streamed_actor).c_str(), Text(ue::ClassOf(streamed_actor)).c_str(),
             cinematic ? 1 : 0, hidden ? 1 : 0);
    }
    return cinematic || hidden;
}
bool LookReady(const Context& context) {
    if (!AnimInstance(context.mesh)) return false;
    if (Property<uint8_t>(Property<Object*>(context.pawn, "CharacterMovement"), "MovementMode") == 0) return false;  // MOVE_None: a cutscene / scripted state
    if (SequencePlaying(context)) return false;
    // The primary being hidden indicates a cinematic/preview or a special pawn state.
    bool visible = false, hidden = true;
    return VisibilityOf(context.mesh, visible, hidden) && ((visible && !hidden) ||
        (source_driver_mode_ && source_settings_owned_ && Same(primary_) && context.mesh==primary_.object));
}
// The game's cutscene copy of the hero (the sequence's spawnable; the player pawn is hidden meanwhile): a Character (or
// a SkeletalMeshActor) other than the player and not of the player's class (NetPuppet's second hero is), whose body mesh
// is on the hero's skeleton, animated and shown. Best first: a BP_Seq_Hun* class, then a class named "Seq", then one
// whose body is the player's own body mesh; unrelated actors require a sequence binding. While none is found, the actors looked
// at are logged once per cutscene (NPC_RETARGET cutscene candidates).
Object* cutscene_logged_world_=nullptr;ULONGLONG cutscene_logged_at_=0;
Object* CutsceneMesh(Object* actor) {
    Object* mesh=Property<Object*>(actor,"Mesh");
    return ue::Alive(mesh)?mesh:Property<Object*>(actor,"SkeletalMeshComponent");
}
// Query the active players once per candidate scan. GetObjectBindings is a
// reflected engine call; no private Sequencer binding-map offsets are read.
std::vector<Object*> CutscenePlayers(const Context& context) {
    std::vector<Object*> players;
    Call actors(ue::Default("/Script/Engine.Default__GameplayStatics"),"GetAllActorsOfClass");
    if(!actors.Set("WorldContextObject",context.world).Set("ActorClass",Class("/Script/LevelSequence.LevelSequenceActor")).Invoke())return players;
    const Array list=actors.Get<Array>("OutActors");
    if(list.num>0&&list.num<=4096&&FaceReadable(list.data,static_cast<size_t>(list.num)*sizeof(Object*)))
        for(int32_t i=0;i<list.num;++i){Object* actor=static_cast<Object**>(list.data)[i];if(!ue::Alive(actor))continue;
            Object* player=Property<Object*>(actor,"SequencePlayer");Call playing(player,"IsPlaying");
            if(ue::Alive(player)&&playing.Invoke()&&playing.Get<bool>("ReturnValue"))players.push_back(player);}
    ue::Free(list.data);return players;
}
bool CutsceneBound(const std::vector<Object*>& players,Object* actor,Object* mesh) {
    for(Object* player:players)for(Object* object:{actor,mesh}){
        Call bindings(player,"GetObjectBindings");if(!bindings.Set("InObject",object).Invoke())continue;
        const Array list=bindings.Get<Array>("ReturnValue");const bool bound=list.data&&list.num>0&&list.num<=4096&&list.max>=list.num;
        ue::Free(list.data);if(bound)return true;}
    return false;
}
Object* CutsceneHero(const Context& context) {
    Object* skeleton = Class(kSourceSkeleton);
    if (!skeleton) return nullptr;
    Object* own_class = context.pawn ? ue::ClassOf(context.pawn) : nullptr;
    Object* own_body = context.mesh ? SkelMesh(context.mesh) : nullptr;
    Object* found = nullptr; int best = -1; std::string seen;
    const auto players=CutscenePlayers(context);
    for (const char* type : {"/Script/Engine.Character", "/Script/Engine.SkeletalMeshActor"}) {
        Object* cls = Class(type);
        if (!cls) continue;
        Call actors(ue::Default("/Script/Engine.Default__GameplayStatics"), "GetAllActorsOfClass");
        if (!actors.Set("WorldContextObject", context.pawn ? context.pawn : context.world).Set("ActorClass", cls).Invoke()) continue;
        const Array list = actors.Get<Array>("OutActors");
        if (list.data && list.num > 0 && list.num <= 4096 && FaceReadable(list.data, static_cast<size_t>(list.num) * sizeof(Object*)))
            for (int32_t i = 0; i < list.num; ++i) {
                Object* actor = static_cast<Object**>(list.data)[i];
                if (actor == context.pawn || ue::IsTemplate(actor) || !ue::Alive(actor) || ue::ClassOf(actor) == own_class) continue;
                Object* mesh = CutsceneMesh(actor);
                if (!ue::Alive(mesh) || Property<Object*>(SkelMesh(mesh), "Skeleton") != skeleton) continue;
                const std::string name = Lower(Text(ue::ClassOf(actor)));
                bool visible = false, hidden = true, actor_hidden = true;
                const bool current=cutscene_host_&&Same(pawn_)&&pawn_.object==actor&&Same(primary_)&&primary_.object==mesh;
                // Our source is deliberately hidden. Actor visibility still belongs to Sequencer.
                const bool shown = Bool(actor,"bHidden",actor_hidden)&&!actor_hidden&&
                    ((current&&Same(display_))||(VisibilityOf(mesh,visible,hidden)&&visible&&!hidden));
                const bool animated = AnimInstance(mesh) != nullptr;
                if (seen.size() < 600) seen += (seen.empty() ? "" : ", ") + Text(ue::ClassOf(actor)) + (shown ? "" : "[hidden]") + (animated ? "" : "[no anim]");
                if (!shown || !animated) continue;
                const bool bound=CutsceneBound(players,actor,mesh);
                const int kind=name.rfind("bp_seq_hun",0)==0?3:name.find("seq")!=std::string::npos?2:SkelMesh(mesh)==own_body?1:bound?0:-1;
                const int score=NpcCutscene::Rank(shown,animated,bound,kind,current);
                if (score > best) { best = score; found = actor; }
            }
        ue::Free(list.data);
    }
    const ULONGLONG now = GetTickCount64();
    if (!found && (context.world != cutscene_logged_world_ || now > cutscene_logged_at_ + 30000)) {
        cutscene_logged_world_ = context.world; cutscene_logged_at_ = now;
        Logf("NPC_RETARGET cutscene candidates on the hero's skeleton (none shown and animated): %s", seen.empty() ? "none" : seen.c_str());
    }
    return found;
}
// Standing still with no montage (cleanup of a failed look and the audit still wait for this).
bool Idle(const Context& context) {
    const HeroMotion motion = MotionOf(context);
    return motion.known && !motion.montage && motion.speed <= 10 && (motion.mode == 1 || motion.mode == 2) && LookReady(context);
}
Object* Add(Object* actor, Object* cls, const Transform& relative = {}) {
    Call add(actor, "AddComponentByClass");
    if (!add.Set("Class", cls).Set("bManualAttachment", true).Set("RelativeTransform", relative).Set("bDeferredFinish", true).Invoke()) return nullptr;
    return add.Get<Object*>("ReturnValue");
}
bool Destroy(Object* component) {
    if (!ue::Alive(component)) return true;
    return Call(component, "K2_DestroyComponent").Set("Object", component).Invoke() && !ue::Alive(component);
}
bool AttachFinish(Object* actor, Object* component, Object* parent, Name socket, const Transform& relative) {
    Call attach(component, "K2_AttachToComponent");
    if (!attach.Set("Parent", parent).Set("SocketName", socket).Set("LocationRule", uint8_t{0}).Set("RotationRule", uint8_t{0}).Set("ScaleRule", uint8_t{0}).Set("bWeldSimulatedBodies", false).Invoke() || !attach.Get<bool>("ReturnValue")) return false;
    if (!Call(actor, "FinishAddComponent").Set("Component", component).Set("bManualAttachment", true).Set("RelativeTransform", relative).Invoke()) return false;
    Transform actual;
    if (Property<Object*>(component, "AttachParent") != parent || !(Property<Name>(component, "AttachSocketName") == socket) || !TransformOf(component, actual)) return false;
    double rotation_dot = 0;
    for (size_t i = 0; i < 4; ++i) rotation_dot += relative.rotation[i] * actual.rotation[i];
    if (std::abs(std::abs(rotation_dot) - 1) > 1e-5) return false;
    for (size_t i = 0; i < 3; ++i)
        if (std::abs(relative.translation[i] - actual.translation[i]) > 0.01 || std::abs(relative.scale[i] - actual.scale[i]) > 1e-5) return false;
    return true;
}
bool CopyMaterials(Object* from, Object* to) {
    const int32_t offset = Offset(to, "OverrideMaterials");
    if (offset < 0) return false;
    const Array source = Property<Array>(from, "OverrideMaterials");
    if (source.num < 0 || source.num > 128 || (source.num && !source.data)) return false;
    // Keep the UE allocation. Backup owns the original references; SetMaterial
    // uses the engine allocator if the destination needs more capacity.
    ue::At<Array>(to, offset).num = 0;
    for (int32_t i = 0; i < source.num; ++i)
        if (!Call(to, "SetMaterial").Set("ElementIndex", i).Set("Material", static_cast<Object**>(source.data)[i]).Invoke()) return false;
    Array& dest = ue::At<Array>(to, offset);
    if (dest.max < source.num) return false;
    // Preserve trailing null override slots too; they are valid UObject nulls.
    if (source.num) std::memcpy(dest.data, source.data, source.num * sizeof(Object*));
    dest.num = source.num; return true;
}
[[maybe_unused]] bool Backup(const Context& context) {
    AnimOptions options;
    if (!ReadAnimOptions(context.mesh, options)) return Fail(L"无法读取原主角 PostProcess 设置，未开始替换");
    Object* backup = Add(context.pawn, Class("/Script/Engine.SkeletalMeshComponent"));
    if (!backup) return Fail(L"无法创建 GC 持有原网格的备份组件");
    backup_ = Id(backup);
    if (!SetMesh(backup, SkelMesh(context.mesh)) || !CopyMaterials(context.mesh, backup) || !Put(backup, "AnimClass", AnimClass(context.mesh)) ||
        !Put(backup, "PhysicsAssetOverride", Property<Object*>(context.mesh, "PhysicsAssetOverride")) ||
        !Put(backup, "OverridePostProcessAnimBP", options.override_class) || !PutBool(backup, "bDisablePostProcessBlueprint", options.disabled) ||
        !Put(backup, "PostProcessAnimBPLODThreshold", options.lod)) return false;
    SetVisible(backup, false, true);
    Call(backup, "SetComponentTickEnabled").Set("bEnabled", false).Invoke();
    // Intentionally do not FinishAddComponent: PostCreateBlueprintComponent put
    // it in the actor's reflected BlueprintCreatedComponents array. It holds the
    // original assets/classes without registering a second PBZ AnimInstance.
    return true;
}
[[maybe_unused]] bool PatchSkeleton(Object* skeleton, const Profile& profile) {
    if (!ue::Alive(skeleton) || ue::Offset(Class("/Script/Engine.Skeleton"), "BoneTree") != 0x38 ||
        ue::Offset(Class("/Script/Engine.Skeleton"), "bUseRetargetModesFromCompatibleSkeleton") != 0x1a0) return Fail(L"Skeleton 原生字段布局不符");
    const Array tree = Property<Array>(skeleton, "BoneTree");
    if (!tree.data || tree.num != profile.bones || tree.max < tree.num || tree.max > 16384) return Fail(L"目标 Skeleton 骨数与审计资料不符");
    auto* data = static_cast<uint8_t*>(tree.data);
    skeleton_edit_.skeleton = Id(skeleton); skeleton_edit_.data = reinterpret_cast<uintptr_t>(data);
    if (!Bool(skeleton, "bUseRetargetModesFromCompatibleSkeleton", skeleton_edit_.old_flag)) return false;
    skeleton_edit_.original.assign(data, data + tree.num); skeleton_edit_.written = skeleton_edit_.original;
    for (uint8_t mode : skeleton_edit_.original) if (mode > 4) return Fail(L"目标 Skeleton 含未知重定向模式");
    for (size_t i = 0; i < profile.shared_count; ++i) {
        const uint16_t index = profile.shared[i]; if (index >= tree.num) return false;
        skeleton_edit_.written[index] = index == 0 ? 0 : 3;
    }
    if (!PutBool(skeleton, "bUseRetargetModesFromCompatibleSkeleton", false)) return Fail(L"无法设置目标骨架的原生重定向开关");
    std::memcpy(data, skeleton_edit_.written.data(), tree.num);
    Logf("NPC_RETARGET native AnimationRelative applied bones=%zu target=%s flag=0", profile.shared_count, Text(skeleton).c_str()); return true;
}
bool RestoreSkeleton() {
    if (!skeleton_edit_.skeleton.object) return true;
    if (!Same(skeleton_edit_.skeleton)) { skeleton_edit_ = SkeletonEdit{}; return true; }
    bool complete = true;
    if (Same(skeleton_edit_.skeleton)) {
        Object* skeleton = skeleton_edit_.skeleton.object;
        const Array tree = Property<Array>(skeleton, "BoneTree");
        if (reinterpret_cast<uintptr_t>(tree.data) == skeleton_edit_.data && static_cast<size_t>(tree.num) == skeleton_edit_.original.size()) {
            auto* modes = static_cast<uint8_t*>(tree.data);
            for (size_t i = 0; i < skeleton_edit_.original.size(); ++i) {
                if (skeleton_edit_.original[i] == skeleton_edit_.written[i]) continue;
                if (modes[i] == skeleton_edit_.written[i]) modes[i] = skeleton_edit_.original[i];
                if (modes[i] == skeleton_edit_.original[i]) skeleton_edit_.written[i] = skeleton_edit_.original[i];
                else complete = false;
            }
        } else { Logf("NPC_RETARGET bone array identity changed; restore skipped"); complete = false; }
        // A false original flag was never changed, so do not undo a later
        // external true value. For an owned true->false edit, true is already
        // the original state and false is the one state we may restore.
        if (skeleton_edit_.old_flag) {
            bool flag = false;
            if (!Bool(skeleton, "bUseRetargetModesFromCompatibleSkeleton", flag)) complete = false;
            else if (!flag && !PutBool(skeleton, "bUseRetargetModesFromCompatibleSkeleton", true)) complete = false;
        }
    }
    if (complete) skeleton_edit_ = SkeletonEdit{};
    else Logf("NPC_RETARGET skeleton restore unresolved; retained owned mode ledger for retry");
    return complete;
}
bool ArrayField(Object* owner, const char* name, uintptr_t& field, Array*& array) {
    field = owner ? Field(ue::ClassOf(owner), name) : 0;
    if (!field || ue::At<int32_t>(reinterpret_cast<void*>(field), ue::layout::kPropElementSize) != static_cast<int32_t>(sizeof(Array))) return false;
    const uintptr_t inner = ue::At<uintptr_t>(reinterpret_cast<void*>(field), 0x78);
    if (!inner || ue::At<int32_t>(reinterpret_cast<void*>(inner), ue::layout::kPropElementSize) != static_cast<int32_t>(sizeof(Object*))) return false;
    array = &ue::At<Array>(owner, ue::At<int32_t>(reinterpret_cast<void*>(field), ue::layout::kPropOffset));
    return array->num >= 0 && array->num <= array->max && array->max <= 65536 && (!array->num || array->data);
}
bool SocketExists(Object* component, Name name) {
    Call exists(component, "DoesSocketExist");
    return exists.Set("InSocketName", name).Invoke() && exists.Get<bool>("ReturnValue");
}
bool RebuildSockets(Object* mesh) {
    if (!kRebuildSocketMapRva) return false;
    reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base() + kRebuildSocketMapRva)(mesh); return true;
}
bool QuiesceSkeleton(Object* skeleton) {
    Object* component_class = Class("/Script/Engine.SkeletalMeshComponent");
    if (!NativeHelpersMatch() || !component_class || ue::StructSize(component_class) < 0xdf0) return false;
    bool ok = true; size_t waited = 0;
    // Exact copy of the wait/complete sequence in this build's native
    // USkeletalMeshComponent::SetSkeletalMesh (3dbb2aa..3dbb2e5). Complete is
    // virtual because the budgeted component wraps it for its timing metrics.
    ue::ForEachObject([&](Object* component) {
        if (ue::IsTemplate(component) || !ue::Alive(component) || !ue::IsA(component, component_class)) return true;
        Object* mesh = SkelMesh(component);
        if (!mesh || Property<Object*>(mesh, "Skeleton") != skeleton) return true;
        void* task = ue::At<void*>(component, 0xde8);
        if (!task) return true;
        if (ue::At<DWORD>(task, 0x20) == GetCurrentThreadId()) { ok = false; return false; }
        const uintptr_t method = ue::At<uintptr_t*>(component, 0)[0xd00 / sizeof(uintptr_t)];
        if (method != ue::Base() + 0x3da9320 && method != ue::Base() + 0x5cc9a30) { ok = false; return false; }
        void* graph = reinterpret_cast<void*(__fastcall*)()>(ue::Base() + 0x1267790)();
        if (!graph) { ok = false; return false; }
        reinterpret_cast<void(__fastcall*)(void*, const void*, uint32_t)>(ue::Base() + 0x1f27810)(graph, component + 0xde8, 1);
        reinterpret_cast<void(__fastcall*)(Object*, bool)>(method)(component, true);
        if (ue::At<void*>(component, 0xde8)) { ok = false; return false; }
        ++waited; return true;
    });
    if (waited) Logf("NPC_RETARGET synchronized %zu target-skeleton animation tasks without resetting instances", waited);
    return ok;
}
bool FaceReadable(const void* pointer, size_t bytes) {
    if (!pointer) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(pointer, &memory, sizeof(memory)) || memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const uintptr_t at = reinterpret_cast<uintptr_t>(pointer), start = reinterpret_cast<uintptr_t>(memory.BaseAddress);
    return at >= start && bytes <= memory.RegionSize && at - start <= memory.RegionSize - bytes;
}
bool ReadMeshReferences(Object* mesh,std::vector<NpcPoseTransfer::Bone>& bones,size_t& raw_count) {
    if(!ue::Alive(mesh)||!ue::IsA(mesh,Class("/Script/Engine.SkeletalMesh"))||!NativeHelpersMatch())return false;
    const uintptr_t method=ue::At<uintptr_t*>(mesh,0)[0x320/8];if(method!=ue::Base()+kGetMeshReferenceRva)return false;
    Object* ref=reinterpret_cast<Object*(__fastcall*)(Object*)>(method)(mesh);if(ref!=mesh+0x2e0||!FaceReadable(ref,0x40))return false;
    const Array raw=ue::At<Array>(ref,0),info=ue::At<Array>(ref,0x20),pose=ue::At<Array>(ref,0x30);
    if(raw.num<1||raw.num>info.num||info.num>2048||info.max<info.num||info.max>4096||pose.num!=info.num||pose.max<pose.num||pose.max>4096||
        !FaceReadable(raw.data,static_cast<size_t>(raw.num)*12)||!FaceReadable(info.data,static_cast<size_t>(info.num)*12)||!FaceReadable(pose.data,static_cast<size_t>(pose.num)*96))return false;
    bones.clear();bones.reserve(static_cast<size_t>(info.num));
    for(int32_t i=0;i<info.num;++i){Object* at=static_cast<Object*>(info.data)+static_cast<size_t>(i)*12;NpcPoseTransfer::Bone bone;
        std::memcpy(&bone.name,at,8);bone.parent=ue::At<int32_t>(at,8);std::memcpy(&bone.ref,static_cast<Object*>(pose.data)+static_cast<size_t>(i)*96,96);
        const Name name=ue::At<Name>(at,0);if(name.None()||!NpcGripMath::Valid(bone.ref)||(i==0?bone.parent!=-1:(bone.parent<0||bone.parent>=i)))return false;
        if(i<raw.num&&std::memcmp(at,static_cast<Object*>(raw.data)+static_cast<size_t>(i)*12,12))return false;bones.push_back(bone);}
    raw_count=static_cast<size_t>(raw.num);return true;
}
int32_t PlanBone(const std::vector<NpcPoseTransfer::Bone>& bones,const char* name);
bool GeometryForAnchor(const NpcPoseTransfer::Plan& plan,int32_t si,int32_t ti,bool hand,bool forearm,double* basis);
// One retarget pose for every body. Each NPC arm starts from the hero's rest directions (upper
// arm, then forearm, each keeping its parent's turn) and each hand from the hero's hand frame,
// matched by finger geometry. The hero's motion then lands the same way on any skeleton, and
// the weapon adapter's hand turn (target hand * basis * inverse(source hand)) becomes identity
// (up to the wrist limit, kWristMatchMaxDeg).
bool MatchArmsToHero(NpcPoseTransfer::Plan& plan) {
    double degrees[2][4]{};bool matched[2]{};
    for(size_t side=0;side<2;++side){const std::string s=side?"r":"l";
        const int32_t upper=PlanBone(plan.target,("upperarm_"+s).c_str()),lower=PlanBone(plan.target,("lowerarm_"+s).c_str()),hand=PlanBone(plan.target,("hand_"+s).c_str()),source_hand=PlanBone(plan.source,("hand_"+s).c_str());
        if(upper<0||lower<0||hand<0||source_hand<0||plan.target_to_source[static_cast<size_t>(hand)]!=source_hand)continue;
        if(!NpcPoseTransfer::AlignRestDirection(plan,upper,lower,&degrees[side][0])||!NpcPoseTransfer::AlignRestDirection(plan,lower,hand,&degrees[side][1]))continue;
        double basis[4],inverse[4],tmp[4],turn[4],wrist[4];if(!GeometryForAnchor(plan,source_hand,hand,true,false,basis))continue;
        NpcGripMath::Inverse(basis,inverse);NpcGripMath::Mul(plan.source_ref_cs[static_cast<size_t>(source_hand)].rotation,inverse,tmp);
        NpcGripMath::Inverse(plan.target_ref_cs[static_cast<size_t>(hand)].rotation,inverse);NpcGripMath::Mul(tmp,inverse,turn);
        NpcGripMath::Inverse(plan.rest_align[static_cast<size_t>(lower)].data(),inverse);NpcGripMath::Mul(inverse,turn,wrist);degrees[side][2]=degrees[side][3]=NpcPoseTransfer::detail::Degrees(wrist);
        // A hand whose palm sits far off the hero's (Scavenger's left, 63 deg) keeps part of its own
        // rest pose; the weapon adapter's basis still seats the weapon in it.
        if(degrees[side][3]>kWristMatchMaxDeg){const double identity[4]={0,0,0,1};double limited[4];NpcPoseTransfer::detail::Slerp(identity,wrist,kWristMatchMaxDeg/degrees[side][3],limited);
            NpcGripMath::Mul(plan.rest_align[static_cast<size_t>(lower)].data(),limited,turn);degrees[side][2]=NpcPoseTransfer::detail::Degrees(limited);}
        matched[side]=NpcPoseTransfer::SetRestTurn(plan,hand,turn);}
    if(!NpcPoseTransfer::FinishRestTurns(plan)){Logf("NPC_BODY arms rest-pose match refused (compose)");plan.rest_set.clear();return NpcPoseTransfer::FinishRestTurns(plan);}
    Logf("NPC_BODY arms matched to hero rest pose l=%d(upper %.1f fore %.1f wrist %.1f/%.1f deg) r=%d(upper %.1f fore %.1f wrist %.1f/%.1f deg)",
        matched[0],degrees[0][0],degrees[0][1],degrees[0][2],degrees[0][3],matched[1],degrees[1][0],degrees[1][1],degrees[1][2],degrees[1][3]);
    return true;
}
// Pose plans by look: the same hero mesh, display mesh and profile give the same plan (bone map, translation modes,
// arm match, weapon scale, IK followers, two-hand contact, mount puppet), so a look worn again copies its plan (and
// the sizes read with it) instead of building it. Rows are checked against the live meshes; the oldest goes first.
struct PlanCacheRow { Identity source_asset,target_asset;std::string profile;NpcPoseTransfer::Plan plan;std::vector<int32_t> virtual_source;
    double skin_size=1,stature=0,camera_lift=0,look_ratio=0;const MountLoops* mount=nullptr;const OwnLocomotion* loco=nullptr;ULONGLONG used=0; };
std::vector<PlanCacheRow> plan_cache_;constexpr size_t kPlanCacheRows=16;
bool SameAsset(const Identity& cached,const Identity& now){return cached.object&&cached.object==now.object&&cached.index==now.index&&cached.serial==now.serial&&Same(cached);}
void InstallPoseSession(std::unique_ptr<PoseSession> session){AcquireSRWLockExclusive(&pose_lock_);pose_session_=std::move(session);ReleaseSRWLockExclusive(&pose_lock_);}
void SessionGenerations(PoseSession& session){
    Object* identities[]={session.source.object,session.source_asset.object,session.target_asset.object};
    for(size_t i=0;i<3;++i)reinterpret_cast<void(__fastcall*)(int32_t*,Object* const*)>(ue::Base()+0x166e900)(&session.generations[i*2],&identities[i]);
}
bool PreparePoseSession(Object* source,Object* target_asset,const Profile& profile) {
    auto session=std::make_unique<PoseSession>();session->source=Id(source);session->source_asset=Id(SkelMesh(source));session->target_asset=Id(target_asset);
    plan_cache_.erase(std::remove_if(plan_cache_.begin(),plan_cache_.end(),[](const PlanCacheRow& row){return !Same(row.source_asset)||!Same(row.target_asset);}),plan_cache_.end());
    for(PlanCacheRow& row:plan_cache_){
        if(row.profile!=profile.id||!SameAsset(row.source_asset,session->source_asset)||!SameAsset(row.target_asset,session->target_asset))continue;
        session->plan=row.plan;session->virtual_source=row.virtual_source;skin_size_=row.skin_size;stature_ratio_=row.stature;camera_lift_=row.camera_lift;look_ratio_=row.look_ratio;
        mount_.row=row.mount;mount_.loco=row.loco;row.used=GetTickCount64();
        SessionGenerations(*session);NpcPoseTransfer::Prepare(session->plan,session->cache);
        Logf("NPC_BODY prepared from the look cache id=%s mapped=%zu targetRuntime=%zu weaponScale=%.3f skinSize=%.3f contact=%d mount=%s",profile.id,session->plan.mapped,
             session->plan.target.size(),session->plan.weapon_scale,skin_size_,session->plan.contact.enabled?1:0,mount_.row?mount_.row->mount_root:"-");
        InstallPoseSession(std::move(session));return true;
    }
    std::vector<NpcPoseTransfer::Bone> source_refs,target_refs;size_t source_raw=0,target_raw=0;std::string why;
    if(!ReadMeshReferences(session->source_asset.object,source_refs,source_raw)||!ReadMeshReferences(target_asset,target_refs,target_raw)||target_raw!=static_cast<size_t>(profile.bones))return false;
    std::vector<Transform> source_cs,target_cs;if(!NpcPoseTransfer::ReferenceCS(source_refs,source_cs,why)||!NpcPoseTransfer::ReferenceCS(target_refs,target_cs,why))return false;
    std::map<uint64_t,size_t> source_names;for(size_t i=0;i<source_refs.size();++i)source_names.emplace(source_refs[i].name,i);
    std::map<std::string,const BodySkeletonBasis*> skeleton_bases;
    const char* skeleton_key=std::strrchr(profile.skeleton,'.');skeleton_key=skeleton_key?skeleton_key+1:profile.skeleton;
    size_t skeleton_index=std::size(kBodySkeletonNames);
    for(size_t i=0;i<std::size(kBodySkeletonNames);++i)if(Lower(kBodySkeletonNames[i])==Lower(skeleton_key)){skeleton_index=i;break;}
    for(const BodySkeletonBasis& row:kBodySkeletonBases)if(row.skeleton==skeleton_index)skeleton_bases.emplace(Lower(row.bone),&row);
    std::vector<std::array<double,4>> q0(target_refs.size(),{0,0,0,1});std::vector<const BodySkeletonBasis*> own_rows(target_refs.size(),nullptr);
    for(size_t t=0;t<target_raw;++t){auto found=source_names.find(target_refs[t].name);if(found==source_names.end())continue;Name name{};std::memcpy(&name,&target_refs[t].name,8);auto basis=skeleton_bases.find(Lower(Text(name)));
        if(basis==skeleton_bases.end())return false;std::copy(std::begin(basis->second->q0),std::end(basis->second->q0),q0[t].begin());own_rows[t]=basis->second;}
    if(!NpcPoseTransfer::Build(session->plan,std::move(source_refs),std::move(target_refs),q0,why,target_raw)){Logf("NPC_BODY plan refused reason=%s",why.c_str());return false;}
    if(!MatchArmsToHero(session->plan))return false;
    session->plan.foreign_keep.assign(session->plan.target.size(),0);
    for(const char* bone:{"root","pelvis"}){const int32_t b=PlanBone(session->plan.target,bone);if(b>=0)session->plan.foreign_keep[static_cast<size_t>(b)]=1;}
    {auto& plan=session->plan;plan.source_mode.assign(plan.target.size(),NpcPoseTransfer::kScaled);size_t modes[3]{};
        for(size_t t=0;t<plan.target.size();++t){const int32_t s=plan.target_to_source[t];if(s<0)continue;Name name;std::memcpy(&name,&plan.source[static_cast<size_t>(s)].name,8);const std::string text=Lower(Text(name));
            for(const char* bone:kSourceAnimationBones)if(text==Lower(bone))plan.source_mode[t]=NpcPoseTransfer::kAnimation;
            for(const char* bone:kSourceSkeletonBones)if(text==bone)plan.source_mode[t]=NpcPoseTransfer::kSkeleton;
            ++modes[plan.source_mode[t]];}
        // The boss's own modes and Skeleton lengths give its own montages back as the boss shows them.
        // Untracked bones keep the display length (kScaled, display length: the candidate 55 result).
        plan.own_mode.assign(plan.target.size(),NpcPoseTransfer::kScaled);plan.own_length.resize(plan.target.size());size_t own_modes[3]{},built_off=0,untracked=0;double built_off_cm=0;
        for(size_t t=0;t<plan.target.size();++t){const double display=NpcPoseTransfer::Length3(plan.target[t].ref.translation);plan.own_length[t]=display;
            const BodySkeletonBasis* row=t<own_rows.size()?own_rows[t]:nullptr;if(!row||plan.target_to_source[t]<0||row->mode>2||!std::isfinite(row->length)||row->length<0)continue;
            if(!row->animated){++untracked;continue;}
            plan.own_mode[t]=row->mode==0?NpcPoseTransfer::kAnimation:row->mode==1?NpcPoseTransfer::kSkeleton:NpcPoseTransfer::kScaled;plan.own_length[t]=row->length;++own_modes[plan.own_mode[t]];
            const double off=std::abs(row->length-display);if(off>0.01*std::max(display,1.0)){++built_off;built_off_cm=std::max(built_off_cm,off);}}
        Logf("NPC_BODY own-montage boss Skeleton modes scaled=%zu animation=%zu skeleton=%zu untracked=%zu; %zu bones of the display mesh built off that Skeleton (up to %.1f cm)",own_modes[0],own_modes[1],own_modes[2],untracked,built_off,built_off_cm);
        // Grabs scale by the body's height: the rest pelvis height over the hero's.
        const int32_t sp=PlanBone(plan.source,"pelvis"),tp=PlanBone(plan.target,"pelvis");
        const double hero=sp>=0?plan.source_ref_cs[static_cast<size_t>(sp)].translation[2]:0,npc=tp>=0?plan.target_ref_cs[static_cast<size_t>(tp)].translation[2]:0;
        skin_size_=hero>1&&npc>1&&std::isfinite(npc/hero)?std::clamp(npc/hero,kWeaponScaleMin,kWeaponScaleMax):1.0;
        Logf("NPC_BODY own-montage translation modes scaled=%zu animation=%zu skeleton=%zu; skin size %.3f (pelvis %.1f / hero %.1f cm)",modes[0],modes[1],modes[2],skin_size_,npc,hero);}
    {const auto size_bones=[](const std::vector<NpcPoseTransfer::Bone>& bones){NpcPoseTransfer::SizeBones b;b.head=PlanBone(bones,"head");
            for(size_t s=0;s<2;++s){const std::string x=s?"_r":"_l";b.upper[s]=PlanBone(bones,("upperarm"+x).c_str());b.lower[s]=PlanBone(bones,("lowerarm"+x).c_str());b.hand[s]=PlanBone(bones,("hand"+x).c_str());
                b.middle[s]=PlanBone(bones,("middle_01"+x).c_str());b.index[s]=PlanBone(bones,("index_01"+x).c_str());b.pinky[s]=PlanBone(bones,("pinky_01"+x).c_str());}return b;};
        NpcPoseTransfer::BodyScale size;const bool sized=NpcPoseTransfer::WeaponScale(session->plan,size_bones(session->plan.source),size_bones(session->plan.target),kWeaponScaleMin,kWeaponScaleMax,size);
        stature_ratio_=size.ratio[0];
        Logf("NPC_BODY weapon scale %s applied=%.3f raw=%.3f ratios(stature %.3f reach %.3f fist %.3f) npcCm(%.1f %.1f %.1f) heroCm(%.1f %.1f %.1f) clamp=%.1f..%.1f",sized?"matched":"unavailable",
            session->plan.weapon_scale,size.raw,size.ratio[0],size.ratio[1],size.ratio[2],size.target[0],size.target[1],size.target[2],size.source[0],size.source[1],size.source[2],kWeaponScaleMin,kWeaponScaleMax);}
    // The NPC post-process AnimBP runs after this pose (twist / cloth bones, and on many bosses a Control
    // Rig weapon or hand IK aimed at its ik_* or virtual bones). Keep those on the hands it now has, so
    // that IK has nothing left to pull: ik_* copies follow the FK bone they rest on, and each virtual
    // bone copies its target bone's slot in the CopyPose map (BodyMap) instead of holding the rest pose.
    {auto& plan=session->plan;std::vector<int32_t> hands;for(const char* h:{"hand_l","hand_r"}){const int32_t b=PlanBone(plan.target,h);if(b>=0)hands.push_back(b);}
        std::vector<int32_t> follow=NpcPoseTransfer::HandProxies(plan.target,plan.target_ref_cs,hands,kHandProxyCm);std::string list;size_t used=0;
        for(size_t t=0;t<follow.size();++t){if(follow[t]<0)continue;Name name;std::memcpy(&name,&plan.target[t].name,8);const std::string text=Text(name);
            if(t>=plan.target_raw||Lower(text).rfind("ik_",0)!=0){follow[t]=-1;continue;}++used;if(list.size()<240)list+=(list.empty()?"":",")+text;}
        const bool followed=NpcPoseTransfer::SetFollowers(plan,follow);
        plan.authored_follow.assign(plan.target.size(),0);
        for(size_t t=0;t<plan.target.size();++t){Name name;std::memcpy(&name,&plan.target[t].name,8);
            if(NpcSkillSupport::AuthoredWeaponIk(profile.id,Lower(Text(name))))plan.authored_follow[t]=1;}
        session->virtual_source.assign(plan.target.size(),-1);size_t driven=0;std::string virtual_list;
        Object* skeleton=Class(profile.skeleton);const Array vbs=Property<Array>(skeleton,"VirtualBones");
        // FVirtualBone = {SourceBoneName, TargetBoneName, VirtualBoneName}; a row counts only if its virtual bone exists here under that source.
        if(skeleton&&vbs.num>0&&vbs.num<=256&&vbs.max>=vbs.num&&FaceReadable(vbs.data,static_cast<size_t>(vbs.num)*24))for(int32_t i=0;i<vbs.num;++i){
            uint64_t row[3];std::memcpy(row,static_cast<Object*>(vbs.data)+static_cast<size_t>(i)*24,24);
            for(size_t t=plan.target_raw;t<plan.target.size();++t){if(plan.target[t].name!=row[2])continue;const int32_t p=plan.target[t].parent;if(p<0||plan.target[static_cast<size_t>(p)].name!=row[0])break;
                for(size_t b=0;b<plan.target_raw;++b)if(plan.target[b].name==row[1]){const int32_t s=plan.target_to_source[b];if(s>=0){session->virtual_source[t]=s;++driven;Name vb;std::memcpy(&vb,&row[2],8);
                    if(virtual_list.size()<240)virtual_list+=(virtual_list.empty()?"":",")+Text(vb);}break;}
                break;}}
        Logf("NPC_BODY post-process inputs: ik followers=%zu set=%d (%s); virtual bones=%zu driven=%zu (%s)",used,followed?1:0,list.c_str(),plan.target.size()-plan.target_raw,driven,virtual_list.c_str());}
    SessionGenerations(*session);
    const auto& built=session->plan;NpcPoseTransfer::HandContact request;
    request.source_hand_l=PlanBone(built.source,"hand_l");request.source_hand_r=PlanBone(built.source,"hand_r");
    request.source_palm_l=PlanBone(built.source,"middle_01_l");request.source_palm_r=PlanBone(built.source,"middle_01_r");
    request.upper=PlanBone(built.target,"upperarm_l");request.lower=PlanBone(built.target,"lowerarm_l");request.hand=PlanBone(built.target,"hand_l");request.hand_r=PlanBone(built.target,"hand_r");
    request.palm_l=PlanBone(built.target,"middle_01_l");request.palm_r=PlanBone(built.target,"middle_01_r");
    // Grip gate: the hero's left fist (weapon_l's rest place in hand_l) must sit on the weapon_r handle.
    request.source_weapon_r=PlanBone(built.source,"weapon_r");const int32_t source_weapon_l=PlanBone(built.source,"weapon_l");
    const bool gate=request.source_weapon_r>=0&&source_weapon_l>=0&&request.source_hand_l>=0&&
        NpcGripMath::Relative(built.source_ref_cs[static_cast<size_t>(source_weapon_l)],built.source_ref_cs[static_cast<size_t>(request.source_hand_l)],request.grip_l);
    // Same hand bases as the weapon adapter (InstallGripHook), so the left hand turns with the weapon.
    const bool bases=gate&&request.source_hand_r>=0&&request.hand>=0&&request.hand_r>=0&&
        GeometryForAnchor(built,request.source_hand_l,request.hand,true,false,request.basis_l)&&GeometryForAnchor(built,request.source_hand_r,request.hand_r,true,false,request.basis_r);
    const bool contact=bases&&NpcPoseTransfer::SetHandContact(session->plan,request);
    // The weapon adapter's right-hand turn at rest, on the NPC's own rest pose and on the matched one (0 when matched).
    double turn_degrees[2]{};
    if(contact)for(size_t k=0;k<2;++k){const auto& cs=k?built.target_rest_cs:built.target_ref_cs;double inverse[4],tmp[4],turn[4];NpcGripMath::Inverse(built.source_ref_cs[static_cast<size_t>(request.source_hand_r)].rotation,inverse);
        NpcGripMath::Mul(cs[static_cast<size_t>(request.hand_r)].rotation,request.basis_r,tmp);NpcGripMath::Mul(tmp,inverse,turn);turn_degrees[k]=NpcPoseTransfer::detail::Degrees(turn);}
    // The pose puppet's always-bones: every raw bone the hero has none of (the boss's own, 玉妖's foot blades; ik_* ones
    // stay as they were, the IK followers own those), a mount (kMountLoops) and on a ridden one the rider's pelvis and
    // legs (every raw bone outside spine_01). Unmapped ones borrow source slots no target bone maps for the CopyPose.
    // Only a look with loops of its own (or a mount) gets a puppet.
    mount_.row=nullptr;mount_.loco=nullptr;
    {auto& plan=session->plan;std::vector<uint8_t> always(plan.target.size(),0);size_t own=0,held=0;
        for(size_t t=1;t<plan.target_raw;++t){if(plan.target_to_source[t]>=0)continue;Name name;std::memcpy(&name,&plan.target[t].name,8);
            if(Lower(Text(name)).rfind("ik_",0)==0)continue;always[t]=1;++own;}
        for(const MountLoops& row:kMountLoops){if(std::strcmp(row.skeleton,profile.skeleton))continue;
            const int32_t mount=PlanBone(plan.target,row.mount_root),upper=PlanBone(plan.target,"spine_01");
            if(mount<0||(row.ridden&&upper<0)){Logf("NPC_MOUNT %s: no %s / spine_01 on the display mesh, no mount",profile.id,row.mount_root);break;}
            for(size_t t=1;t<plan.target_raw;++t){bool in_mount=false,in_upper=false;
                for(int32_t bone=static_cast<int32_t>(t);bone>=0;bone=plan.target[static_cast<size_t>(bone)].parent){in_mount=in_mount||bone==mount;in_upper=in_upper||bone==upper;}
                if((in_mount||(row.ridden&&!in_upper))&&!always[t]){always[t]=1;++held;}}
            mount_.row=&row;break;}
        for(const OwnLocomotion& row:kOwnLocomotion)if(!std::strcmp(row.profile,profile.id)){
            bool any=row.idle!=nullptr;for(size_t d=0;d<4;++d)any=any||row.walk[d]||row.run[d];if(any)mount_.loco=&row;break;}
        size_t assigned=0,wanted=0;
        if(!mount_.row&&!mount_.loco)Logf("NPC_LOCO %s: no locomotion of its own, no puppet (%zu bones the hero has none of keep their reference pose)",profile.id,own);
        else if(!NpcPoseTransfer::SetPuppet(plan,always,assigned,wanted)){Logf("NPC_LOCO %s: puppet plan refused",profile.id);mount_.row=nullptr;mount_.loco=nullptr;}
        else{plan.puppet_own=mount_.row!=nullptr;plan.extra_mesh=NpcSkillSupport::MeshSpaceWeaponCopy(profile.id);
            Logf("NPC_LOCO %s: puppet always-bones %zu (the hero has none of %zu; mount %s adds %zu, ridden=%d); borrowed source slots %zu of %zu unmapped; mount whole body in own montages=%d meshSlots=%d",
                profile.id,own+held,own,mount_.row?mount_.row->mount_root:"-",held,mount_.row?mount_.row->ridden:0,assigned,wanted,plan.puppet_own?1:0,plan.extra_mesh?1:0);}}
    NpcPoseTransfer::Prepare(session->plan,session->cache);
    const auto& k=session->plan.contact;
    Logf("NPC_BODY two-hand contact %s weaponFrame=1 refTurnDeg native=%.1f matched=%.1f fullCm=%.0f noneCm=%.0f gripGate=%d(weapon_r axis %.0f..%.0f cm, %.0f..%.0f deg)",contact?"enabled":"unavailable",turn_degrees[0],turn_degrees[1],
        k.full_cm,k.none_cm,gate,k.grip_full_cm,k.grip_none_cm,k.axis_full_deg,k.axis_none_deg);
    const int32_t source_head=PlanBone(built.source,"head"),target_head=PlanBone(built.target,"head");
    camera_lift_=source_head>=0&&target_head>=0?built.target_ref_cs[static_cast<size_t>(target_head)].translation[2]-built.source_ref_cs[static_cast<size_t>(source_head)].translation[2]:0;
    {const int32_t source_pelvis=PlanBone(built.source,"pelvis"),target_pelvis=PlanBone(built.target,"pelvis");
        const auto z=[](const auto& cs,int32_t bone){return bone>=0&&static_cast<size_t>(bone)<cs.size()?cs[static_cast<size_t>(bone)].translation[2]:0.0;};
        look_ratio_=NpcHeroSize::LookRatio(z(built.source_ref_cs,source_head),z(built.target_ref_cs,target_head),z(built.source_ref_cs,source_pelvis),z(built.target_ref_cs,target_pelvis));
        Logf("NPC_SIZE %s height over the hero's %.3f (head %.1f / %.1f cm, pelvis %.1f / %.1f cm); 主角体型 shows it x%.3f",profile.id,look_ratio_,
            z(built.target_ref_cs,target_head),z(built.source_ref_cs,source_head),z(built.target_ref_cs,target_pelvis),z(built.source_ref_cs,source_pelvis),NpcHeroSize::Target(look_ratio_));}
    Logf("NPC_BODY prepared sourceRaw=%zu sourceRuntime=%zu targetRaw=%zu targetRuntime=%zu mapped=%zu parentMismatchKept=%zu sourceGeneration=%s target=%s sharedAssetWrites=0",
        source_raw,session->plan.source.size(),target_raw,session->plan.target.size(),session->plan.mapped,session->plan.parent_mismatch,Text(session->source_asset.object).c_str(),Text(target_asset).c_str());
    {PlanCacheRow row;row.source_asset=session->source_asset;row.target_asset=session->target_asset;row.profile=profile.id;row.plan=session->plan;row.virtual_source=session->virtual_source;
        row.skin_size=skin_size_;row.stature=stature_ratio_;row.camera_lift=camera_lift_;row.look_ratio=look_ratio_;row.mount=mount_.row;row.loco=mount_.loco;row.used=GetTickCount64();
        if(plan_cache_.size()>=kPlanCacheRows)plan_cache_.erase(std::min_element(plan_cache_.begin(),plan_cache_.end(),[](const PlanCacheRow& a,const PlanCacheRow& b){return a.used<b.used;}));
        plan_cache_.push_back(std::move(row));}
    InstallPoseSession(std::move(session));return true;
}
bool SourceCurrentCS(Object* source,const Transform*& transforms,size_t& count) {
    if(!source||!FaceReadable(source,0x650))return false;const int32_t read=ue::At<int32_t>(source,0x640);if(read<0||read>1)return false;
    const Array current=ue::At<Array>(source,0x5f8+read*16);
    if(current.num<1||current.num>2048||current.max<current.num||current.max>4096||!FaceReadable(current.data,static_cast<size_t>(current.num)*96))return false;
    transforms=static_cast<const Transform*>(current.data);count=static_cast<size_t>(current.num);return true;
}
bool UpdatePoseCache(const Transform* source,size_t count) {
    if(!pose_session_)return false;const char* why="ok";
    // The mount puppet's finished pose (the display ticks after it), while it is the display mesh.
    NpcPoseTransfer::PuppetPose puppet;const NpcPoseTransfer::PuppetPose* posed=nullptr;const Identity& id=pose_session_->puppet;
    if(id.object&&Same(id)&&SkelMesh(id.object)==pose_session_->target_asset.object&&SourceCurrentCS(id.object,puppet.cs,puppet.count)){
        puppet.serial=mount_serial_.load(std::memory_order_relaxed);puppet.now_ms=GetTickCount64();puppet.body=loco_body_.load(std::memory_order_relaxed);posed=&puppet;}
    if(!NpcPoseTransfer::Evaluate(pose_session_->plan,source,count,pose_session_->cache,why,foreign_weight_.load(std::memory_order_relaxed),foreign_own_.load(std::memory_order_relaxed),posed)){pose_session_->why=why;InterlockedExchange(&pose_session_->failed,1);return false;}
    pose_session_->why="ok";return true;
}
bool RawIdentity(Identity identity, int32_t index, int32_t serial) {
    if (!identity.object || index<0 || serial<=0 || !FaceReadable(identity.object,0x28) ||
        index>=ue::detail::ObjectCount() || ue::detail::ObjectAt(index)!=identity.object) return false;
    auto* array=reinterpret_cast<Object*>(ue::Base()+ue::layout::kObjectArray);
    Object** chunks=ue::At<Object**>(array,24); const uint32_t chunk=static_cast<uint32_t>(index)>>16;
    if (!FaceReadable(chunks,(static_cast<size_t>(chunk)+1)*8)||!chunks[chunk]) return false;
    Object* item=chunks[chunk]+(static_cast<uint32_t>(index)&65535)*24ull;
    return FaceReadable(item,24) && ue::At<int32_t>(item,16)==serial && ue::NameOf(identity.object)==identity.name;
}
bool PoseIdentityValid() {
    if(!pose_session_)return false;const auto& p=*pose_session_;return RawIdentity(p.source,p.generations[0],p.generations[1])&&RawIdentity(p.source_asset,p.generations[2],p.generations[3])&&RawIdentity(p.target_asset,p.generations[4],p.generations[5]);
}
bool FaceRawInstance(const FaceHook& hook) { return RawIdentity(hook.instance,hook.instance_index,hook.instance_serial); }
bool FaceQuiesce(Object* component) {
    if (!Same(Id(component)) || !NativeHelpersMatch()) return false;
    void* task = ue::At<void*>(component, 0xde8); if (!task) return true;
    if (!FaceReadable(task, 0x24) || ue::At<DWORD>(task, 0x20) == GetCurrentThreadId()) return false;
    const uintptr_t method = ue::At<uintptr_t*>(component, 0)[0xd00 / 8];
    if (method != ue::Base() + 0x3da9320 && method != ue::Base() + 0x5cc9a30) return false;
    void* graph = reinterpret_cast<void*(__fastcall*)()>(ue::Base() + 0x1267790)(); if (!graph) return false;
    reinterpret_cast<void(__fastcall*)(void*, const void*, uint32_t)>(ue::Base() + 0x1f27810)(graph, component + 0xde8, 1);
    reinterpret_cast<void(__fastcall*)(Object*, bool)>(method)(component, true);
    return !ue::At<void*>(component, 0xde8);
}
bool FaceNativeMatch() {
    static constexpr unsigned char pre[] = {0x40,0x53,0x55,0x56,0x48,0x83,0xec,0x40,0x48,0x8b,0xf1,0x48,0x8b,0xea,0x48,0x8b};
    static constexpr unsigned char evaluate[] = {0x4c,0x8b,0xdc,0x56,0x57,0x41,0x54,0x48,0x81,0xec,0xc0,0,0,0,0x48,0x8b};
    static constexpr unsigned char weak[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x12,0x48,0x8b,0xd9,0x48,0x85,0xd2,0x74};
    return ue::Base() && !std::memcmp(reinterpret_cast<void*>(ue::Base()+0x35d7d30),pre,sizeof(pre)) &&
        !std::memcmp(reinterpret_cast<void*>(ue::Base()+0x35bed60),evaluate,sizeof(evaluate)) &&
        !std::memcmp(reinterpret_cast<void*>(ue::Base()+0x166e900),weak,sizeof(weak));
}
bool FaceMap(FaceHook& hook, bool restore) {
    Object* node = hook.node; const Profile& profile = *hook.profile;
    auto refuse=[&](const char* state) { hook.map_state=state; return false; };
    if (restore && !hook.map_written) { hook.map_state="no-owned-map-writes"; return true; }
    if (profile.head_bones < 16 || profile.head_bones > 2048 || !profile.facial_count || hook.saved.size() != static_cast<size_t>(profile.head_bones) || !FaceReadable(node, 344)) return refuse("node-layout");
    // Exact PE: BoneMapToSource@48, TSet pair stride16, int key@0/value@4.
    // Change values only. UE still owns allocation, hash chains, keys/capacity.
    Object* map = node + 0x48; void* data = ue::At<void*>(map,0);
    const int32_t slots = ue::At<int32_t>(map,8), capacity = ue::At<int32_t>(map,12), bits = ue::At<int32_t>(map,40),
        max_bits = ue::At<int32_t>(map,44), free = ue::At<int32_t>(map,52);
    const uint64_t source_mesh = ue::At<uint64_t>(node,0x2c), target_mesh = ue::At<uint64_t>(node,0x3c);
    const bool same_epoch=data==hook.map_data && slots==hook.map_slots && source_mesh==hook.source_mesh && target_mesh==hook.target_mesh;
    // Native may replace or clear the map while retaining this AnimInstance.
    // That new allocation/mesh epoch contains no writes owned by this ledger.
    // Check identity before shape, including a valid native empty-map reset.
    if (restore && !same_epoch) { hook.map_written=false; hook.map_state="native-map-epoch-retired"; return true; }
    const int32_t expected=hook.runtime_bones?hook.runtime_bones:profile.head_bones;
    if (expected<profile.head_bones || expected>2048 || slots<expected || slots>2048 || capacity<slots || capacity>4096 || bits!=slots || max_bits<bits ||
        free<0 || slots-free!=expected || !FaceReadable(data,static_cast<size_t>(slots)*16)) return refuse("map-shape");
    const uint32_t* allocated = ue::At<uint32_t*>(map,32);
    if (!allocated) { if (bits>128) return refuse("map-inline-bitmap"); allocated=reinterpret_cast<uint32_t*>(map+16); }
    if (!FaceReadable(allocated,((static_cast<size_t>(bits)+31)/32)*4)) return refuse("map-bitmap");
    std::array<bool,2048> facial{}; std::array<bool,2048> seen{}; std::array<Object*,2048> entries{};
    for (size_t i=0;i<profile.facial_count;++i) { if (profile.facial_bones[i]>=profile.head_bones) return refuse("profile-facial-index"); facial[profile.facial_bones[i]]=true; }
    size_t filtered=0;
    for (int32_t i=0;i<slots;++i) if (allocated[i/32] & (1u << (i%32))) {
        Object* entry = static_cast<Object*>(data)+static_cast<size_t>(i)*16;
        const int32_t key=ue::At<int32_t>(entry,0), value=ue::At<int32_t>(entry,4);
        if (key<0 || key>=expected || seen[key]) return refuse("map-duplicate-or-range"); seen[key]=true;
        // Runtime reference skeletons can append virtual bones. Keep them and
        // their source mapping intact; only the audited facial keys change.
        if (key>=profile.head_bones) continue;
        if (!facial[key]) { if (!restore && key<10 && value<0) return refuse("head-neck-source-missing"); continue; }
        entries[key]=entry; ++filtered;
    }
    if (filtered!=profile.facial_count || !std::all_of(seen.begin(),seen.begin()+expected,[](bool value){return value;})) return refuse("map-incomplete");
    // Validate every key/hash/bitmap before committing any facial write. A
    // failed initial installation must have no half-written map to unwind.
    if (!restore && !same_epoch) {
        std::fill(hook.saved.begin(),hook.saved.end(),std::numeric_limits<int32_t>::min()); hook.map_data=data; hook.map_slots=slots;
        hook.source_mesh=source_mesh; hook.target_mesh=target_mesh;
    }
    for (size_t key=0;key<static_cast<size_t>(profile.head_bones);++key) if (Object* entry=entries[key]) {
        int32_t& value=ue::At<int32_t>(entry,4);
        if (restore) {
            if (value == -1 && hook.saved[key] != std::numeric_limits<int32_t>::min()) value=hook.saved[key];
        } else {
            if (value != -1 || hook.saved[key] == std::numeric_limits<int32_t>::min()) hook.saved[key]=value;
            value=-1; // Evaluate's explicit negative-index branch keeps this bone's mesh reference pose.
        }
    }
    hook.map_written=!restore; hook.map_state=restore?"owned-map-restored":"facial-filtered"; return true;
}
bool BodyMap(FaceHook& hook,bool restore) {
    if(restore&&!hook.map_written)return true;if(!pose_session_||!FaceReadable(hook.node,344)){hook.map_state="body-map-node";return false;}
    Object* map=hook.node+0x48;void* data=ue::At<void*>(map,0);const int32_t slots=ue::At<int32_t>(map,8),capacity=ue::At<int32_t>(map,12),bits=ue::At<int32_t>(map,40),maxbits=ue::At<int32_t>(map,44),free=ue::At<int32_t>(map,52);
    const uint64_t source=ue::At<uint64_t>(hook.node,0x2c),target=ue::At<uint64_t>(hook.node,0x3c);const bool same=data==hook.map_data&&slots==hook.map_slots&&source==hook.source_mesh&&target==hook.target_mesh;
    if(restore&&!same){hook.map_written=false;hook.map_state="body-map-epoch-retired";return true;}
    const auto& plan=pose_session_->plan;const size_t count=plan.target.size();if(count!=static_cast<size_t>(hook.runtime_bones)||hook.body_saved.size()!=count||slots<static_cast<int32_t>(count)||slots>2048||capacity<slots||capacity>4096||bits!=slots||maxbits<bits||free<0||slots-free!=static_cast<int32_t>(count)||!FaceReadable(data,static_cast<size_t>(slots)*16)){hook.map_state="body-map-layout";return false;}
    const uint32_t* allocated=ue::At<uint32_t*>(map,32);if(!allocated){if(bits>128){hook.map_state="body-map-bits";return false;}allocated=reinterpret_cast<uint32_t*>(map+16);}if(!FaceReadable(allocated,((static_cast<size_t>(bits)+31)/32)*4)){hook.map_state="body-map-bits-read";return false;}
    // virtuals: the rows written here - virtual bones, and unmapped bones on a borrowed source slot (Plan::extra_source).
    std::array<bool,2048> seen{};std::array<Object*,2048> virtuals{};const bool extras=plan.extra_source.size()==count;
    for(int32_t i=0;i<slots;++i)if(allocated[i/32]&(1u<<(i%32))){Object* row=static_cast<Object*>(data)+static_cast<size_t>(i)*16;const int32_t key=ue::At<int32_t>(row,0),value=ue::At<int32_t>(row,4);
        if(key<0||static_cast<size_t>(key)>=count||seen[static_cast<size_t>(key)]){hook.map_state="body-map-key";return false;}seen[static_cast<size_t>(key)]=true;
        // Also rewritten: a raw bone the plan leaves unmapped while the engine's by-name map points it at a same-name
        // source bone (a bone under a different parent: Plan::parent_mismatch, 毕峰 / GreatBladeMan's) - it keeps the NPC
        // reference pose (-1) or its borrowed puppet slot.
        if(static_cast<size_t>(key)>=plan.target_raw||(extras&&plan.extra_source[static_cast<size_t>(key)]>=0)||plan.target_to_source[static_cast<size_t>(key)]<0)virtuals[static_cast<size_t>(key)]=row;
        else if(!restore&&value!=plan.target_to_source[static_cast<size_t>(key)]){hook.map_state="body-map-value";Logf("NPC_BODY CopyPose map row target=%d source=%d plan=%d",key,value,plan.target_to_source[static_cast<size_t>(key)]);return false;}}
    if(!std::all_of(seen.begin(),seen.begin()+count,[](bool b){return b;})){hook.map_state="body-map-missing";return false;}
    if(!restore&&!same){std::fill(hook.body_saved.begin(),hook.body_saved.end(),std::numeric_limits<int32_t>::min());hook.map_data=data;hook.map_slots=slots;hook.source_mesh=source;hook.target_mesh=target;}
    // A virtual bone copies its target bone's source slot (PreparePoseSession), else -1 keeps its reference pose;
    // a mount bone reads its borrowed slot.
    bool wrote=false,driven=false;for(size_t i=0;i<count;++i)if(virtuals[i]){int32_t& value=ue::At<int32_t>(virtuals[i],4);
        const int32_t want=i<plan.target_raw?(extras?plan.extra_source[i]:-1):pose_session_->virtual_source.size()==count?pose_session_->virtual_source[i]:-1;if(want>=0)driven=true;
        if(restore){if(value==want&&hook.body_saved[i]!=std::numeric_limits<int32_t>::min())value=hook.body_saved[i];}
        else{if(value!=want||hook.body_saved[i]==std::numeric_limits<int32_t>::min())hook.body_saved[i]=value;value=want;wrote=true;}}
    hook.map_written=!restore&&wrote;hook.map_state=restore?"body-virtual-map-restored":driven?"body-raw-map-verified-virtual-driven":"body-raw-map-verified-virtual-ref";return true;
}
// hook.map_state names the step that refused (the install log prints it).
bool RewriteBodyCache(FaceHook& hook) {
    if(!PoseIdentityValid()){hook.map_state="body-identity";return false;}
    if(!Same(pose_session_->source)||!Same(pose_session_->source_asset)||!Same(pose_session_->target_asset)||
        SkelMesh(pose_session_->source.object)!=pose_session_->source_asset.object||SkelMesh(hook.component.object)!=pose_session_->target_asset.object){hook.map_state="body-session-assets";return false;}
    if(!BodyMap(hook,false)){if(!std::strcmp(hook.map_state,"unseen"))hook.map_state="body-map";return false;}
    const Array cache=ue::At<Array>(hook.node,0xe8);
    if(cache.num<1||cache.max<cache.num||cache.max>4096||!FaceReadable(cache.data,static_cast<size_t>(cache.num)*96)){hook.map_state="body-node-cache";return false;}
    const Transform* current=nullptr;size_t count=0;
    if(!SourceCurrentCS(pose_session_->source.object,current,count)){hook.map_state="body-source-pose";return false;}
    if(count!=static_cast<size_t>(cache.num)){hook.map_state="body-cache-count";Logf("NPC_BODY CopyPose cache %d bones, source pose %zu",cache.num,count);return false;}
    bool ok=false;AcquireSRWLockExclusive(&pose_lock_);
    __try { ok=UpdatePoseCache(current,count);if(ok)std::memcpy(cache.data,pose_session_->cache.synthetic_source.data(),static_cast<size_t>(cache.num)*96); }
    __finally { ReleaseSRWLockExclusive(&pose_lock_); }
    if(!ok)hook.map_state="body-pose-update";
    return ok;
}
void FacePreUpdateBody(Object* node, Object* instance) {
    FaceHook* hook = nullptr;
    for (const auto& candidate:face_hooks_) if (candidate->node==node && candidate->instance.object==instance) { hook=candidate.get(); break; }
    if (!hook) { reinterpret_cast<void(__fastcall*)(Object*,Object*)>(ue::Base()+0x35d7d30)(node,instance); return; }
    if (GetCurrentThreadId()!=face_thread_) { InterlockedExchange(&hook->failed,1); return; }
    if(hook->source_input)ue::At<uint64_t>(node,0x10)=hook->source_input;
    // PreUpdate is the engine's GT stage before parallel graph evaluation.
    // Reassert before native caching; then filter every rebuilt map afterward.
    ue::At<uint8_t>(node,0x18) &= static_cast<uint8_t>(~2u);
    ue::At<uint8_t>(node,0x19) = 0;
    if(hook->mesh_space)ue::At<uint8_t>(node,0x1a)|=1;
    reinterpret_cast<void(__fastcall*)(Object*,Object*)>(ue::Base()+0x35d7d30)(node,instance);
    if(hook->source_input&&(ue::At<uint64_t>(node,0x24)!=hook->source_input||ue::At<uint64_t>(node,0x34)!=hook->source_asset_input)){InterlockedExchange(&hook->failed,1);return;}
    const LONGLONG cache_started=hook->body?PerfStart():0;const bool mapped=hook->body?RewriteBodyCache(*hook):FaceMap(*hook,false);if(hook->body)PerfEnd(PerfKind::BodyCache,cache_started);
    if(!mapped){InterlockedExchange(&hook->failed,1);return;}
    if(hook->body){if(++hook->epochs==1)Logf("NPC_BODY CopyPose private owned cache retarget active mapped=%zu targetRaw=%zu targetRuntime=%zu nativeSourceUnchanged=1",pose_session_->plan.mapped,pose_session_->plan.target_raw,pose_session_->plan.target.size());return;}
    if (++hook->epochs==1) Logf("NPC_FACE_NEUTRAL source=%s mappedFacial=0 meshRefFacial=%zu headNeckCopied=10 runtimeBones=%d rawBones=%d curves=0 attributes=0 private-node=1",hook->profile->id,hook->profile->facial_count,hook->runtime_bones,hook->profile->head_bones);
}
void __fastcall FacePreUpdate(Object* node, Object* instance) {
    const LONGLONG started=PerfStart();bool found=false,body=false;if(started)for(const auto& candidate:face_hooks_)if(candidate->node==node){found=true;body=candidate->body;break;}
    __try { FacePreUpdateBody(node,instance); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        for (const auto& candidate:face_hooks_) if (candidate->node==node) InterlockedExchange(&candidate->failed,1);
    }
    if(found)PerfEnd(body?PerfKind::BodyPre:PerfKind::FacePre,started);
}
bool InstallFaceHook(Object* component, const Profile& profile,bool body=false) {
    if (!FaceNativeMatch() || !FaceQuiesce(component)) return false;
    Object* instance=AnimInstance(component), *type=Class("/Script/AnimGraphRuntime.AnimNode_CopyPoseFromMesh");
    const uintptr_t field=instance?Field(ue::ClassOf(instance),"AnimGraphNode_CopyPoseFromMesh"):0;
    if (!instance || ue::IsTemplate(instance) || !type || ue::StructSize(type)!=344 || ue::Offset(type,"SourceMeshComponent")!=0x10 ||
        ue::Offset(type,"bCopyCurves")!=0x18 || ue::Offset(type,"bCopyCustomAttributes")!=0x19 ||
        !field || ue::At<Object*>(reinterpret_cast<void*>(field),0x70)!=type || ue::At<int32_t>(reinterpret_cast<void*>(field),0x34)!=344) return false;
    Object* node=instance+ue::At<int32_t>(reinterpret_cast<void*>(field),0x44);
    uintptr_t* original=ue::At<uintptr_t*>(node,0);
    if (original!=reinterpret_cast<uintptr_t*>(ue::Base()+0xae44180) || original[3]!=ue::Base()+0x35bed60 ||
        original[8]!=ue::Base()+0x35d7d30 || original[14]!=ue::Base()+0x35a7fb0 ||
        !FaceReadable(original-1,21*8) || (ue::At<uint8_t>(node,0x1a)&1)) return false;
    auto hook=std::make_unique<FaceHook>(); hook->component=Id(component); hook->instance=Id(instance);hook->body=body;
    hook->mesh_space=body&&NpcSkillSupport::MeshSpaceWeaponCopy(profile.id);
    if(hook->mesh_space){
        const uintptr_t mesh_pose=Field(type,"bUseMeshPose");
        if(!mesh_pose||ue::At<int32_t>(reinterpret_cast<void*>(mesh_pose),ue::layout::kPropOffset)!=0x1a||
            ue::At<uint8_t>(reinterpret_cast<void*>(mesh_pose),0x71)!=0||ue::At<uint8_t>(reinterpret_cast<void*>(mesh_pose),0x73)!=1)return false;
        // The private cache already contains the target component-space pose. Bone-space CopyPose
        // subtracts the source weapon's HAND parent when mapping it onto a root-parented virtual bone.
        // The fire post-process then treats that hand-local value as a world-space weapon target.
        // Native mesh-space copying retains the correct root-relative virtual target instead.
        Logf("NPC_BODY fire virtual weapons: native mesh-space CopyPose enabled id=%s",profile.id);
    }
    int32_t weak[2]{};
    reinterpret_cast<void(__fastcall*)(int32_t*,Object* const*)>(ue::Base()+0x166e900)(weak,&instance);
    hook->instance_index=weak[0]; hook->instance_serial=weak[1]; hook->node=node; hook->original=original; hook->profile=&profile;
    if (!FaceRawInstance(*hook)) return false;
    reinterpret_cast<void(__fastcall*)(int32_t*,Object* const*)>(ue::Base()+0x166e900)(weak,&component);
    hook->component_index=weak[0]; hook->component_serial=weak[1];
    Call bone_count(component,"GetNumBones");
    if (!bone_count.Slot("ReturnValue",sizeof(int32_t)) || !bone_count.Invoke()) return false;
    hook->runtime_bones=bone_count.Get<int32_t>("ReturnValue");
    if (hook->runtime_bones<(body?profile.bones:profile.head_bones) || hook->runtime_bones>2048) return false;
    if(body){if(!pose_session_||static_cast<size_t>(hook->runtime_bones)!=pose_session_->plan.target.size())return false;hook->body_saved.assign(static_cast<size_t>(hook->runtime_bones),std::numeric_limits<int32_t>::min());}
    if(source_driver_mode_){Object* source=body?primary_.object:display_.object;if(!ue::Alive(source))return false;hook->old_source_input=ue::At<uint64_t>(node,0x10);
        reinterpret_cast<void(__fastcall*)(int32_t*,Object* const*)>(ue::Base()+0x166e900)(weak,&source);std::memcpy(&hook->source_input,weak,8);
        Object* asset=SkelMesh(source);reinterpret_cast<void(__fastcall*)(int32_t*,Object* const*)>(ue::Base()+0x166e900)(weak,&asset);std::memcpy(&hook->source_asset_input,weak,8);}
    hook->old_curve=ue::At<uint8_t>(node,0x18)&2; hook->old_attributes=ue::At<uint8_t>(node,0x19);
    if (hook->old_attributes>1) return false;
    hook->saved.assign(static_cast<size_t>(profile.head_bones),std::numeric_limits<int32_t>::min()); hook->table=std::make_unique<uintptr_t[]>(21);
    std::memcpy(hook->table.get(),original-1,21*8); hook->table[1+8]=reinterpret_cast<uintptr_t>(&FacePreUpdate);
    face_thread_=GetCurrentThreadId(); FaceHook* record=hook.get(); face_hooks_.push_back(std::move(hook));
    ue::At<uintptr_t*>(node,0)=record->table.get()+1;
    FacePreUpdate(node,instance); // Build/filter while registered and still hidden, before any new parallel evaluation.
    if (record->epochs!=1 || InterlockedCompareExchange(&record->failed,0,0))
        Logf("NPC_FACE_NEUTRAL install refused source=%s runtimeBones=%d map=%s ownedMapWrites=%d",profile.id,record->runtime_bones,record->map_state,record->map_written?1:0);
    return record->epochs==1 && !InterlockedCompareExchange(&record->failed,0,0);
}
bool FaceRestorePending(FaceHook& hook, const char* reason) {
    const ULONGLONG now=GetTickCount64();
    if (!hook.restore_reason || std::strcmp(hook.restore_reason,reason) || now-hook.restore_log_time>=2000) {
        hook.restore_reason=reason; hook.restore_log_time=now;
        Logf("NPC_FACE_NEUTRAL restore pending reason=%s component=%s instance=%s map=%s ownedMapWrites=%d",reason,
            Text(hook.component.name).c_str(),Text(hook.instance.name).c_str(),hook.map_state,hook.map_written?1:0);
    }
    return false;
}
void RetireFaceHook(size_t index) {
    if (face_hooks_[index]->table) retired_face_tables_.push_back(std::move(face_hooks_[index]->table));
    face_hooks_.erase(face_hooks_.begin()+index);
}
bool RestoreFaceHooks() {
    for (size_t i=face_hooks_.size();i-- >0;) {
        FaceHook& hook=*face_hooks_[i];
        if (!FaceRawInstance(hook)) { RetireFaceHook(i); continue; }
        // IsValid/Alive can reject an old, detached pending-GC instance even
        // though its GU slot and node are still physically present. Restore
        // that exact old instance, never the component's replacement instance.
        if (!FaceReadable(hook.node,344)) return FaceRestorePending(hook,"old-node-unreadable");
        const bool component_raw=RawIdentity(hook.component,hook.component_index,hook.component_serial);
        if (component_raw) {
            if (!FaceReadable(hook.component.object,0xdf0)) return FaceRestorePending(hook,"component-task-unreadable");
            if (ue::At<void*>(hook.component.object,0xde8) &&
                (!Same(hook.component) || !FaceQuiesce(hook.component.object))) return FaceRestorePending(hook,"component-task-pending");
        }
        // An owned component whose GU slot retired has already passed native
        // destruction/task completion. No engine call on that stale object.
        uintptr_t* current=ue::At<uintptr_t*>(hook.node,0);
        if (!Same(hook.instance) && current!=hook.table.get()+1 && current!=hook.original)
            return FaceRestorePending(hook,"pending-GC-node-table-changed");
        if (!(hook.body?BodyMap(hook,true):FaceMap(hook,true))) return FaceRestorePending(hook,"owned-map-restore");
        if(hook.source_input&&ue::At<uint64_t>(hook.node,0x10)==hook.source_input)ue::At<uint64_t>(hook.node,0x10)=hook.old_source_input;
        uint8_t& curves=ue::At<uint8_t>(hook.node,0x18); if (!(curves&2)) curves|=hook.old_curve;
        if (!ue::At<uint8_t>(hook.node,0x19)) ue::At<uint8_t>(hook.node,0x19)=hook.old_attributes;
        if(hook.mesh_space)ue::At<uint8_t>(hook.node,0x1a)&=static_cast<uint8_t>(~1u); // install requires original bit clear
        const char* table_result=current==hook.original?"already-original":"foreign-table-preserved";
        if (current==hook.table.get()+1) {
            void* previous=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(hook.node),hook.original,current);
            table_result=previous==current?"owned-CAS-restored":"foreign-CAS-race-preserved";
        }
        Logf("NPC_FACE_NEUTRAL restored instance=%s map=%s vtable=%s",Text(hook.instance.name).c_str(),hook.map_state,table_result);
        RetireFaceHook(i);
    }
    face_visibility_.clear(); return true;
}
// The look's first evaluations get 3 s of game time. A paused world (the hub's 暂停游戏, the game's own menu) evaluates no
// animation, so the time it stays paused does not count: a look put on from the open hub waits for the game to go on
// instead of being taken off again after 3 s.
bool GamePaused() {
    if (!Same(pawn_)) return false;
    Call paused(ue::Default("/Script/Engine.Default__GameplayStatics"), "IsGamePaused");
    return paused.Slot("ReturnValue", sizeof(bool)) && paused.Set("WorldContextObject", pawn_.object).Invoke() && paused.Get<bool>("ReturnValue");
}
bool FaceWaitLeft() {
    const ULONGLONG now = GetTickCount64();
    if (now > face_wait_seen_ && GamePaused()) face_wait_started_ += now - face_wait_seen_;
    face_wait_seen_ = now; return now - face_wait_started_ < 3000;
}
bool RefreshFaceVisibility() {
    for (const auto& hook:face_hooks_) {
        if (InterlockedCompareExchange(&hook->failed,0,0) || !Same(hook->component) || !Same(hook->instance) ||
            AnimInstance(hook->component.object)!=hook->instance.object || ue::At<uintptr_t*>(hook->node,0)!=hook->table.get()+1) return false;
        if (!face_visibility_.empty()) {
            if (hook->epochs<2) return FaceWaitLeft();
            if (!FaceQuiesce(hook->component.object)) return FaceWaitLeft();
        }
    }
    if (face_visibility_.empty()) return true;
    for (const Visibility& visibility:face_visibility_) if (Same(visibility.component)) {
        Object* component=visibility.component.object;
        if (!SetVisible(component,visibility.visible,visibility.hidden)) return false;
        if (visibility.visible&&!visibility.hidden) {
            if (ue::IsA(component,Class("/Script/HairStrandsCore.GroomComponent")))
                reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base()+ue::layout::kApplyGroomHairMode)(component);
            else if (ue::IsA(component,Class("/Script/Engine.SkinnedMeshComponent")))
                reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base()+ue::layout::kApplySkinnedHairMode)(component);
        }
    }
    face_visibility_.clear(); Logf("NPC_FACE_NEUTRAL visible after native parallel evaluation completion; head/children follow NPC mesh-ref + own DNA");
    return true;
}
const BoneFallback* Fallback(const Profile& profile, Name bone) {
    for (size_t i = 0; i < profile.fallback_count; ++i) if (ue::NameIs(bone, profile.fallbacks[i].bone)) return &profile.fallbacks[i];
    return nullptr;
}
const GripBasis* Grip(const Profile& profile, Name bone) {
    for (size_t i = 0; i < profile.grip_count; ++i) if (ue::NameIs(bone, profile.grips[i].bone)) return &profile.grips[i];
    return nullptr;
}
Name RawGripName(Name bone);
using NativeSocketTransform = Transform*(__fastcall*)(Object*, Transform*, Name, uint32_t);
using NativeSocketInfo = Object*(__fastcall*)(Object*, Name, Transform*, int32_t*);
NativeSocketTransform OriginalSocketTransform() { return reinterpret_cast<NativeSocketTransform>(ue::Base()+kSocketTransformRva); }
NativeSocketInfo OriginalSocketInfo() { return reinterpret_cast<NativeSocketInfo>(ue::Base()+kSocketInfoRva); }

int32_t PlanBone(const std::vector<NpcPoseTransfer::Bone>& bones,const char* name) {
    for(size_t i=0;i<bones.size();++i){Name n;std::memcpy(&n,&bones[i].name,8);if(ue::NameIs(n,name))return static_cast<int32_t>(i);}return -1;
}
bool GeometryForAnchor(const NpcPoseTransfer::Plan& plan,int32_t si,int32_t ti,bool hand,bool forearm,double* basis) {
    const char* suffix="r";Name n;std::memcpy(&n,&plan.source[static_cast<size_t>(si)].name,8);const std::string name=Lower(Text(n));if(!name.empty()&&name.back()=='l')suffix="l";
    std::array<std::string,4> points;
    if(hand)points={std::string("middle_metacarpal_")+suffix,std::string("middle_01_")+suffix,std::string("index_01_")+suffix,std::string("pinky_01_")+suffix};
    else if(forearm)points={name,std::string("hand_")+suffix,std::string("index_01_")+suffix,std::string("pinky_01_")+suffix};
    else points={"spine_01","spine_05","clavicle_l","clavicle_r"};
    double frames[2][3][3]{};
    for(size_t side=0;side<2;++side){const auto& bones=side?plan.target:plan.source;const auto& cs=side?plan.target_rest_cs:plan.source_ref_cs;const int32_t anchor=side?ti:si;Transform p[4];
        for(size_t k=0;k<4;++k){const int32_t index=PlanBone(bones,points[k].c_str());if(index<0||!NpcGripMath::Relative(cs[static_cast<size_t>(index)],cs[static_cast<size_t>(anchor)],p[k]))return false;}
        if(!NpcGripMath::GeometryFrame(p[0].translation,p[1].translation,p[2].translation,p[3].translation,frames[side]))return false;}
    return NpcGripMath::GeometryBasis(frames[0],frames[1],basis);
}
bool ObjectHasExactPath(Object* object,const char* path) {
    // The object is already in hand. Calling FindObject for an absent weapon
    // class here would scan the whole GU array for every child on every poll.
    if(!ue::Alive(object)||!path)return false;const char* dot=std::strrchr(path,'.');if(!dot||!ue::NameIs(ue::NameOf(object),dot+1))return false;
    Object* package=ue::OuterOf(object);if(!ue::Alive(package)||ue::OuterOf(package))return false;
    const size_t length=static_cast<size_t>(dot-path);if(length>=512)return false;char package_name[512]{};std::memcpy(package_name,path,length);
    return ue::NameIs(ue::NameOf(package),package_name);
}
void RefreshForearmBindings() {
    PerfScope timing(PerfKind::ForearmPoll);
    if(!grip_hook_||!Same(primary_))return;
    std::array<ForearmBinding,2> bindings;Object* owner_component=nullptr;AcquireSRWLockShared(&grip_lock_);if(grip_hook_){bindings=grip_hook_->forearms;owner_component=grip_hook_->component.object;}ReleaseSRWLockShared(&grip_lock_);if(!owner_component)return;
    for(ForearmBinding& binding:bindings){binding.enabled=false;binding.reason="not-equipped";if(!binding.basis)continue;
        const Array children=Property<Array>(primary_.object,"AttachChildren");if(children.num<0||children.num>512||children.max<children.num||!children.data)continue;
        for(int32_t i=0;i<children.num;++i){Object* child=static_cast<Object**>(children.data)[i];if(!ue::Alive(child))continue;Call owner(child,"GetOwner");if(!owner.Invoke())continue;Object* actor=owner.Get<Object*>("ReturnValue");
            if(!ue::Alive(actor)||!ObjectHasExactPath(ue::ClassOf(actor),binding.basis->actor_class))continue;Object* mesh=Property<Object*>(actor,"WeaponMesh");bool visible=false,hidden=true;
            if(!ue::Alive(mesh)||!ObjectHasExactPath(SkelMesh(mesh),binding.basis->mesh)||!VisibilityOf(mesh,visible,hidden)||!visible||hidden||!HasNoLeader(mesh)||AnimClass(mesh)||AnimInstance(mesh))continue;
            Call count(mesh,"GetNumBones");if(!count.Invoke()||count.Get<int32_t>("ReturnValue")!=2)continue;
            binding.actor=Id(actor);binding.mesh=Id(mesh);binding.enabled=true;binding.reason="exact-rigid-2bone-glove";binding.leader=Offset(mesh,"LeaderPoseComponent");binding.anim_class=Offset(mesh,"AnimClass");binding.anim_instance=Offset(mesh,"AnimScriptInstance");break;}}
    AcquireSRWLockExclusive(&grip_lock_);if(grip_hook_&&grip_hook_->component.object==owner_component)grip_hook_->forearms=bindings;ReleaseSRWLockExclusive(&grip_lock_);
}
bool RigidForearmEnabled(const GripHook& hook,int32_t source) {
    Name name;std::memcpy(&name,&pose_session_->plan.source[static_cast<size_t>(source)].name,8);
    for(const ForearmBinding& b:hook.forearms)if(b.enabled&&b.bone==name&&Same(b.actor)&&Same(b.mesh)&&b.leader>=0&&b.anim_class>=0&&b.anim_instance>=0){const uint64_t leader=ue::At<uint64_t>(b.mesh.object,b.leader);
        if((leader==0||leader==0x00000000ffffffffull)&&!ue::At<Object*>(b.mesh.object,b.anim_class)&&!ue::At<Object*>(b.mesh.object,b.anim_instance))return true;}return false;
}
int32_t SourceBoneNamed(const NpcPoseTransfer::Plan& plan,Name name){uint64_t key=0;std::memcpy(&key,&name,8);return NpcPoseTransfer::BoneNamed(plan.source,key);}
bool AdjustGripSocket(GripHook& hook,Object* self,Name name,uint32_t space,Transform& out,const Transform& component_space,const Transform& native_mount) {
    if(space>2||!pose_session_)return true;if(!PoseIdentityValid()||!NpcGripMath::Valid(component_space))return false;bool managed=false;int32_t named_bone=-1;
    for(size_t i=0;i<hook.count;++i)if(hook.sockets[i].name==name){managed=true;named_bone=hook.sockets[i].bone;break;}if(!managed)return true;
    // The socket lookup knows only sockets: for a bare bone name (hand weapons hang on WEAPON_R /
    // weapon_l, gloves on lowerarm_l/r) it gives -1, and those weapons used to stay at the hero's.
    Transform local;int32_t bone=-1;OriginalSocketInfo()(self,name,&local,&bone);if(bone<0)bone=named_bone;
    const auto& plan=pose_session_->plan;if(bone<0||static_cast<size_t>(bone)>=plan.source.size())return true;
    int32_t anchor=bone;bool hand_chain=false;const int32_t start=hook.anchors[static_cast<size_t>(bone)].proxy>=0?hook.anchors[static_cast<size_t>(bone)].proxy:bone;
    for(int32_t b=start;b>=0;b=plan.source[static_cast<size_t>(b)].parent)if(hook.anchors[static_cast<size_t>(b)].hand){anchor=b;hand_chain=true;break;}
    if(!hand_chain)while(anchor>=0&&hook.anchors[static_cast<size_t>(anchor)].target<0)anchor=plan.source[static_cast<size_t>(anchor)].parent;
    if(anchor<0)return true;const GripAnchor& row=hook.anchors[static_cast<size_t>(anchor)];
    if(row.forearm&&!RigidForearmEnabled(hook,anchor))return true;
    const Transform* source=nullptr;size_t count=0;if(!SourceCurrentCS(self,source,count))return false;
    bool ok=false;AcquireSRWLockExclusive(&pose_lock_);
    __try {if(UpdatePoseCache(source,count)){
        Transform result;
        const double shown=hero_size_shown_.load(std::memory_order_relaxed);  // 主角体型: the display is shown that much smaller
        ok=NpcGripMath::MapSourceMountPose(source,pose_session_->cache.target_cs.data(),static_cast<size_t>(anchor),static_cast<size_t>(row.target),row.palm,row.target_palm,native_mount,row.basis,component_space,result,
            pose_session_->plan.weapon_scale*shown,shown);
        if(ok){out=result;InterlockedIncrement(&hook.queries);}} }
    __finally {ReleaseSRWLockExclusive(&pose_lock_);}return ok;
}
bool DisplayRouted(const GripHook& hook,Name name) {
    if(!hook.display.object)return false;uint64_t key=0;std::memcpy(&key,&name,8);
    if(std::binary_search(hook.display_only.begin(),hook.display_only.end(),key))return true;
    return GetTickCount64()<skill_route_until_.load(std::memory_order_relaxed)&&std::binary_search(hook.shared.begin(),hook.shared.end(),key);
}
// The display's own socket, in the space asked of the hero mesh it hangs on.
bool DisplaySocketTransform(Object* self,Object* display,Name name,uint32_t space,Transform& out) {
    Transform world,component,local,in_actor;OriginalSocketTransform()(display,&world,name,0);
    if(space==0){out=world;return NpcGripMath::FiniteMount(out);}
    OriginalSocketTransform()(self,&component,Name{},0);if(!NpcGripMath::RelativeNativeSafe(world,component,local))return false;
    if(space==2){out=local;return true;}
    OriginalSocketTransform()(self,&in_actor,Name{},1);return NpcGripMath::ComposeMount(local,in_actor,out);
}
void RouteToDisplay(Object* self,Transform* out,Name name,uint32_t space) {
    AcquireSRWLockShared(&grip_lock_);
    __try {__try {Transform routed;if(grip_hook_&&grip_hook_->component.object==self&&grip_hook_->display.object&&Same(grip_hook_->display)&&
                         DisplaySocketTransform(self,grip_hook_->display.object,name,space,routed)){*out=routed;InterlockedIncrement(&grip_hook_->display_queries);}}
        __except(EXCEPTION_EXECUTE_HANDLER){}}
    __finally {ReleaseSRWLockShared(&grip_lock_);}
}
Transform* __fastcall GripSocketTransform(Object* self,Transform* out,Name name,uint32_t space) {
    OriginalSocketTransform()(self,out,name,space);if(space>2)return out;
    bool managed=false,display=false;AcquireSRWLockShared(&grip_lock_);
    if(grip_hook_&&grip_hook_->component.object==self){for(size_t i=0;i<grip_hook_->count;++i)if(grip_hook_->sockets[i].name==name){managed=true;break;}if(!managed)display=DisplayRouted(*grip_hook_,name);}
    ReleaseSRWLockShared(&grip_lock_);if(display){RouteToDisplay(self,out,name,space);return out;}if(!managed)return out;
    const LONGLONG started=PerfStart();Transform component_space,native_mount=*out;if(space!=2){OriginalSocketTransform()(self,&native_mount,name,2);OriginalSocketTransform()(self,&component_space,Name{},space);}
    AcquireSRWLockShared(&grip_lock_);
    __try {__try {if(grip_hook_&&grip_hook_->component.object==self&&!AdjustGripSocket(*grip_hook_,self,name,space,*out,component_space,native_mount))InterlockedIncrement(&grip_hook_->failures);}
        __except(EXCEPTION_EXECUTE_HANDLER){if(grip_hook_&&grip_hook_->component.object==self)InterlockedIncrement(&grip_hook_->failures);}}
    __finally {ReleaseSRWLockShared(&grip_lock_);}PerfEnd(PerfKind::Grip,started);return out;
}
bool InstallGripHook(Object* component,const Profile& profile) {
    if(grip_hook_||!pose_session_||!FaceQuiesce(component))return false;
    BoneMotionRuntime::BeforeComponentHook(component);
    auto hook=std::make_unique<GripHook>();hook->component=Id(component);const auto& plan=pose_session_->plan;hook->anchors.resize(plan.source.size());
    double torso_root[4],torso_cs[4],inv_root[4],tmp[4];if(!GeometryForAnchor(plan,0,0,false,false,torso_root))return false;NpcGripMath::Inverse(plan.source_ref_cs[0].rotation,inv_root);NpcGripMath::Mul(plan.target_rest_cs[0].rotation,torso_root,tmp);NpcGripMath::Mul(tmp,inv_root,torso_cs);
    for(size_t si=0;si<plan.source.size();++si){GripAnchor& a=hook->anchors[si];a.target=plan.source_to_target[si];if(a.target<0)continue;Name name;std::memcpy(&name,&plan.source[si].name,8);
        a.hand=ue::NameIs(name,"hand_l")||ue::NameIs(name,"hand_r");a.forearm=ue::NameIs(name,"lowerarm_l")||ue::NameIs(name,"lowerarm_r");
        if(a.hand||a.forearm){if(!GeometryForAnchor(plan,static_cast<int32_t>(si),a.target,a.hand,a.forearm,a.basis))return false;}else{double qi[4],qt[4];NpcGripMath::Inverse(plan.target_rest_cs[static_cast<size_t>(a.target)].rotation,qi);NpcGripMath::Mul(qi,torso_cs,qt);NpcGripMath::Mul(qt,plan.source_ref_cs[si].rotation,a.basis);}
        if(a.hand){const char* palm=ue::NameIs(name,"hand_r")?"middle_01_r":"middle_01_l";a.palm=PlanBone(plan.source,palm);a.target_palm=PlanBone(plan.target,palm);if(a.palm<0||a.target_palm<0)return false;}}
    {std::vector<int32_t> hands;for(const char* h:{"hand_l","hand_r"}){const int32_t b=PlanBone(plan.source,h);if(b>=0)hands.push_back(b);}
        const std::vector<int32_t> proxies=NpcPoseTransfer::HandProxies(plan.source,plan.source_ref_cs,hands,kHandProxyCm);std::string list;size_t proxied=0;
        for(size_t si=0;si<proxies.size();++si){if(proxies[si]<0)continue;Name name;std::memcpy(&name,&plan.source[si].name,8);const std::string text=Text(name);
            // Only IK copies, or bones the NPC lacks: a mapped twist bone resting at the wrist keeps its own frame.
            if(Lower(text).rfind("ik_",0)!=0&&plan.source_to_target[si]>=0)continue;
            hook->anchors[si].proxy=proxies[si];++proxied;if(list.size()<240)list+=(list.empty()?"":",")+text;}
        Logf("NPC_GRIP hand proxies=%zu (%s) restToleranceCm=%.1f",proxied,list.c_str(),kHandProxyCm);}
    size_t bare=0;
    for(const char* route:kWeaponAttachmentNames){if(hook->count>=hook->sockets.size())return false;const std::wstring wide(route,route+std::strlen(route));const Name name=ue::MakeName(wide.c_str());
        if(!SocketExists(component,name)){Logf("NPC_GRIP source mesh lacks optional mount=%s; route left native",route);continue;}
        GripSocket& row=hook->sockets[hook->count++];row.name=name;row.bone=SourceBoneNamed(plan,name);if(row.bone>=0)++bare;}
    for(const ForearmBasis& b:kForearmBases)if(Lower(b.profile)==Lower(profile.id)){const size_t side=std::strstr(b.bone,"_r")?0:1;hook->forearms[side].basis=&b;const std::wstring wide(b.bone,b.bone+std::strlen(b.bone));hook->forearms[side].bone=ue::MakeName(wide.c_str());}
    if(!hook->count)return false;uintptr_t* original=ue::At<uintptr_t*>(component,0);size_t count=0;
    if(!FaceReadable(original-1,(kSocketTransformSlot+2)*8)||original[kSocketTransformSlot]!=ue::Base()+kSocketTransformRva)return false;
    while(count<1024&&original[count]>=ue::Base()+0x1000&&original[count]<ue::Base()+0xa600000)++count;
    if(count<=kSocketTransformSlot||!FaceReadable(original-1,(count+1)*8))return false;
    auto* table=static_cast<uintptr_t*>(VirtualAlloc(nullptr,(count+1)*8,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!table)return false;
    std::memcpy(table,original-1,(count+1)*8);table[1+kSocketTransformSlot]=reinterpret_cast<uintptr_t>(&GripSocketTransform);hook->original=original;hook->table=table+1;retired_grip_tables_.push_back(table);
    AcquireSRWLockExclusive(&grip_lock_);grip_hook_=std::move(hook);const bool installed=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(component),grip_hook_->table,original)==original;if(!installed)grip_hook_.reset();ReleaseSRWLockExclusive(&grip_lock_);
    if(installed){RefreshForearmBindings();Logf("NPC_GRIP source driver current-pose adapter installed routes=%zu bareBoneRoutes=%zu weaponScale=%.3f nativeChildPreserved=1 targetAssetsMutated=0",grip_hook_->count,bare,plan.weapon_scale);}return installed;
}
bool RestoreGripHook() {
    AcquireSRWLockExclusive(&grip_lock_);
    if (!grip_hook_) { ReleaseSRWLockExclusive(&grip_lock_); return true; }
    GripHook& hook=*grip_hook_;
    const char* vtable_result="component-retired";
    if (Same(hook.component)) {
        uintptr_t* current=ue::At<uintptr_t*>(hook.component.object,0);
        if (current==hook.table) {
            void* previous=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(hook.component.object),hook.original,hook.table);
            vtable_result=previous==hook.table?"owned-CAS-restored":"foreign-CAS-race-preserved";
        } else vtable_result=current==hook.original?"already-original":"foreign-table-preserved";
        // A foreign private table is preserved. If it copied this function, its
        // calls are now safe native passthrough because the session is retired.
    }
    Logf("NPC_GRIP restored current-pose adapter queries=%ld failures=%ld routes=%zu displayQueries=%ld vtable=%s",hook.queries,hook.failures,hook.count,hook.display_queries,vtable_result);
    grip_hook_.reset(); ReleaseSRWLockExclusive(&grip_lock_); weapon_children_.clear(); weapon_log_due_=0; return true;
}
// Game thread, after InstallGripHook. Bones and sockets of both meshes (GetAllSocketNames). Names the
// hero lacks always go to the display; shared ones only during NPC skills, never the camera, IK,
// interaction or root names (the game aims and plants feet with those).
std::vector<uint64_t> SocketKeys(Object* mesh) {
    std::vector<uint64_t> keys;Call all(mesh,"GetAllSocketNames");if(!all.Invoke())return keys;
    const Array names=all.Get<Array>("ReturnValue");
    if(names.data&&names.num>0&&names.num<=8192)for(int32_t i=0;i<names.num;++i){uint64_t key=0;std::memcpy(&key,static_cast<Name*>(names.data)+i,8);if(key)keys.push_back(key);}
    ue::Free(names.data);std::sort(keys.begin(),keys.end());keys.erase(std::unique(keys.begin(),keys.end()),keys.end());return keys;
}
struct DisplayRouteCache {
    Identity source,target;std::vector<uint64_t> hero_keys,display_keys,only,shared;ULONGLONG used=0;
};
std::vector<DisplayRouteCache> display_route_cache_;
void BuildDisplayRoutes(Object* hero,Object* display) {
    const std::vector<uint64_t> hero_keys=SocketKeys(hero),display_keys=SocketKeys(display);
    const Identity source=Id(SkelMesh(hero)),target=Id(SkelMesh(display));
    display_route_cache_.erase(std::remove_if(display_route_cache_.begin(),display_route_cache_.end(),[](const DisplayRouteCache& row){return !Same(row.source)||!Same(row.target);}),display_route_cache_.end());
    for(auto& cached:display_route_cache_)if(SameAsset(cached.source,source)&&SameAsset(cached.target,target)&&cached.hero_keys==hero_keys&&cached.display_keys==display_keys){
        cached.used=GetTickCount64();AcquireSRWLockExclusive(&grip_lock_);
        if(grip_hook_){grip_hook_->display=Id(display);grip_hook_->display_only=cached.only;grip_hook_->shared=cached.shared;}
        ReleaseSRWLockExclusive(&grip_lock_);Logf("NPC_GRIP display routes cached heroNames=%zu displayNames=%zu",hero_keys.size(),display_keys.size());return;
    }
    std::vector<uint64_t> only,shared;std::string sample;
    for(uint64_t key:display_keys){Name name;std::memcpy(&name,&key,8);const std::string text=Lower(Text(name));
        if(!std::binary_search(hero_keys.begin(),hero_keys.end(),key)){only.push_back(key);if(sample.size()<200&&text.find("bone")==std::string::npos)sample+=(sample.empty()?"":",")+Text(name);continue;}
        if(text.rfind("ik_",0)==0||text.find("cam")!=std::string::npos||text=="root"||text=="interaction"||text=="center_of_mass")continue;
        shared.push_back(key);}
    if(display_route_cache_.size()>=kPlanCacheRows)display_route_cache_.erase(std::min_element(display_route_cache_.begin(),display_route_cache_.end(),[](const DisplayRouteCache& a,const DisplayRouteCache& b){return a.used<b.used;}));
    display_route_cache_.push_back({source,target,hero_keys,display_keys,only,shared,GetTickCount64()});
    AcquireSRWLockExclusive(&grip_lock_);
    if(grip_hook_){grip_hook_->display=Id(display);grip_hook_->display_only=std::move(only);grip_hook_->shared=std::move(shared);
        Logf("NPC_GRIP display routes heroNames=%zu displayNames=%zu displayOnly=%zu sharedDuringSkills=%zu (e.g. %s)",hero_keys.size(),display_keys.size(),grip_hook_->display_only.size(),grip_hook_->shared.size(),sample.empty()?"-":sample.c_str());}
    ReleaseSRWLockExclusive(&grip_lock_);
}
// Game thread. A switched or Shaqi weapon can hang on a mount the generated list lacks (Hun's
// ik_weapon_r copies, knife sockets): every socket another actor's weapon is attached to on the source
// mesh becomes a route only for native weapon actors (NpcGripRoutes::TakesRoute). The pawn's own parts stay
// native, and so does every effect component (FXSystemComponent) and whatever the engine's effect pools own
// (the WorldSettings actor): a pooled burst on foot_r_Socket once became a route, so the foot socket the game
// reads was remapped onto the NPC body (crash 11-06 13:53:43, hub paused). A discovered route goes again once
// no weapon hangs on its socket. Weapons attached anywhere on the pawn are logged once per change, with their
// distance to the NPC hands.
void RefreshWeaponRoutes() {
    if(!grip_hook_||!Same(primary_)||!Same(pawn_))return;
    Object* world_class=Class("/Script/Engine.WorldSettings");Object* fx_class=Class("/Script/Engine.FXSystemComponent");
    Object* weapon_class=Class("/Script/PBZCombatAsset.WeaponActorBase"),*weapon_plain=Class("/Script/PBZCombatAsset.WeaponActorBase_NoSkeletalMesh");
    std::vector<std::pair<Object*,uint64_t>> seen;std::vector<NpcGripRoutes::Attached> on_primary;
    for(Object* parent:Components(pawn_.object,Class("/Script/Engine.SceneComponent"))){
        const Array children=Property<Array>(parent,"AttachChildren");if(children.num<0||children.num>512||children.max<children.num||(children.num&&!children.data))continue;
        for(int32_t i=0;i<children.num;++i){Object* child=static_cast<Object**>(children.data)[i];if(!ue::Alive(child))continue;
            Call owner(child,"GetOwner");if(!owner.Invoke())continue;Object* actor=owner.Get<Object*>("ReturnValue");
            NpcGripRoutes::Attached a;a.owner_alive=ue::Alive(actor);a.owner_is_pawn=actor==pawn_.object;
            a.owner_is_world=a.owner_alive&&world_class&&ue::IsA(actor,world_class);a.effect=fx_class&&ue::IsA(child,fx_class);
            a.weapon_actor=a.owner_alive&&((weapon_class&&ue::IsA(actor,weapon_class))||(weapon_plain&&ue::IsA(actor,weapon_plain)));
            const Name socket=Property<Name>(child,"AttachSocketName");std::memcpy(&a.socket,&socket,8);
            if(parent==primary_.object)on_primary.push_back(a);
            if(!NpcGripRoutes::Weapon(a))continue;
            seen.emplace_back(child,a.socket);
            if(parent!=primary_.object||socket.None())continue;
            bool managed=false;AcquireSRWLockShared(&grip_lock_);for(size_t k=0;grip_hook_&&k<grip_hook_->count;++k)if(grip_hook_->sockets[k].name==socket){managed=true;break;}ReleaseSRWLockShared(&grip_lock_);
            if(!NpcGripRoutes::TakesRoute(a,!managed&&SocketExists(primary_.object,socket),managed))continue;
            size_t routes=0;AcquireSRWLockExclusive(&grip_lock_);const bool added=grip_hook_&&grip_hook_->count<grip_hook_->sockets.size();
            if(added){GripSocket& row=grip_hook_->sockets[grip_hook_->count];row=GripSocket{};row.name=socket;row.bone=pose_session_?SourceBoneNamed(pose_session_->plan,socket):-1;row.found=true;++grip_hook_->count;}
            routes=grip_hook_?grip_hook_->count:0;ReleaseSRWLockExclusive(&grip_lock_);
            Logf("NPC_GRIP route discovered socket=%s owner=%s class=%s child=%s added=%d routes=%zu",Text(socket).c_str(),Text(actor).c_str(),Text(ue::ClassOf(actor)).c_str(),
                 Text(ue::ClassOf(child)).c_str(),added?1:0,routes);}}
    {
        // Discovered routes whose weapon is gone (swapped back, put away): the socket reads native again.
        std::vector<std::string> dropped;size_t routes=0;AcquireSRWLockExclusive(&grip_lock_);
        if(grip_hook_){
            std::vector<NpcGripRoutes::Route> list(grip_hook_->count);
            for(size_t k=0;k<grip_hook_->count;++k){std::memcpy(&list[k].socket,&grip_hook_->sockets[k].name,8);list[k].found=grip_hook_->sockets[k].found;}
            for(const size_t k:NpcGripRoutes::StaleRoutes(list,on_primary)){
                dropped.push_back(Text(grip_hook_->sockets[k].name));
                for(size_t m=k;m+1<grip_hook_->count;++m)grip_hook_->sockets[m]=grip_hook_->sockets[m+1];
                grip_hook_->sockets[--grip_hook_->count]=GripSocket{};}
            routes=grip_hook_->count;}
        ReleaseSRWLockExclusive(&grip_lock_);
        for(const std::string& name:dropped)Logf("NPC_GRIP route dropped socket=%s: no weapon hangs there any more routes=%zu",name.c_str(),routes);
    }
    std::sort(seen.begin(),seen.end());
    if(seen!=weapon_children_){weapon_children_=std::move(seen);weapon_log_due_=GetTickCount64()+500;return;}
    if(!weapon_log_due_||GetTickCount64()<weapon_log_due_)return;weapon_log_due_=0;
    Vector hand[2]{};bool have[2]{};const wchar_t* hand_names[2]={L"hand_r",L"hand_l"};
    if(Same(display_))for(size_t h=0;h<2;++h){Call at(display_.object,"GetSocketLocation");if(at.Set("InSocketName",ue::MakeName(hand_names[h])).Invoke()){hand[h]=at.Get<Vector>("ReturnValue");have[h]=true;}}
    for(const auto& [child,key]:weapon_children_){if(!ue::Alive(child))continue;Name socket;std::memcpy(&socket,&key,8);
        Call owner(child,"GetOwner"),parent(child,"GetAttachParent"),where(child,"K2_GetComponentLocation"),size(child,"K2_GetComponentScale");
        if(!owner.Invoke()||!parent.Invoke()||!where.Invoke()||!size.Invoke())continue;const Vector at=where.Get<Vector>("ReturnValue"),scale=size.Get<Vector>("ReturnValue");Object* attached=parent.Get<Object*>("ReturnValue");
        double nearest=-1;for(size_t h=0;h<2;++h)if(have[h]){const double d=std::sqrt((at.x-hand[h].x)*(at.x-hand[h].x)+(at.y-hand[h].y)*(at.y-hand[h].y)+(at.z-hand[h].z)*(at.z-hand[h].z));if(nearest<0||d<nearest)nearest=d;}
        bool managed=false;int32_t bare=-1;AcquireSRWLockShared(&grip_lock_);for(size_t k=0;grip_hook_&&k<grip_hook_->count;++k)if(grip_hook_->sockets[k].name==socket){managed=true;bare=grip_hook_->sockets[k].bone;break;}ReleaseSRWLockShared(&grip_lock_);
        const char* role=attached==primary_.object?"hero-mesh":attached==display_.object?"npc-display":"other";
        // Melee hits are sweeps of the weapon actor's own shapes ("Attack Collider", read by the
        // BulletData's SweepTrace); their world scale is the attack reach the NPC weapon really has.
        std::string shapes;for(Object* shape:Components(owner.Get<Object*>("ReturnValue"),Class("/Script/Engine.ShapeComponent"))){
            Call shape_scale(shape,"K2_GetComponentScale");if(!shape_scale.Invoke()||shapes.size()>200)continue;char item[96]{};
            _snprintf_s(item,sizeof(item),_TRUNCATE,"%s%s:%.3f",shapes.empty()?"":",",Text(shape).c_str(),shape_scale.Get<Vector>("ReturnValue").x);shapes+=item;}
        Logf("NPC_GRIP weapon child=%s owner=%s parent=%s(%s) socket=%s managed=%d bone=%d nearestNpcHandCm=%.1f worldScale=%.3f hitShapes=%s",Text(child).c_str(),Text(owner.Get<Object*>("ReturnValue")).c_str(),
            Text(attached).c_str(),role,Text(socket).c_str(),managed&&attached==primary_.object?1:0,bare,nearest,scale.x,shapes.empty()?"-":shapes.c_str());}
}
Name RawGripName(Name bone) {
    const std::string text="PBZ_NpcGripRaw_"+Text(bone); const std::wstring wide(text.begin(),text.end());
    return ue::MakeName(wide.c_str());
}
bool AddSocket(Name socket_name, Name old_bone, Transform local, Object* target_mesh, Object* target_skeleton, Object* probe, const Profile& profile, bool raw_alias=false) {
    if (socket_name.None() || old_bone.None()) return false;
    Name bone = old_bone;
    // Resolve a source-only child before selecting its live grip driver. In
    // particular weapon_r_child is an identity child of WEAPON_R in Hun and is
    // absent from both NPC meshes; treating it as a static non-grip socket left
    // weapons using that direct bone name outside the dynamic adapter.
    if (const BoneFallback* fallback = Fallback(profile,old_bone)) {
        const std::wstring ancestor(fallback->ancestor,fallback->ancestor+std::strlen(fallback->ancestor));bone=ue::MakeName(ancestor.c_str());
        if (bone.None() || !Compose(local,fallback->relative,local)) return false;
    }
    const Transform source_local=local;
    const GripBasis* grip = raw_alias?nullptr:Grip(profile,bone);
    if (grip) {
        Transform source_hand;
        if (!Compose(local, grip->source_to_hand, source_hand) || !Relative(source_hand, grip->target_to_hand, local)) return false;
        Logf("NPC_RETARGET grip basis socket=%s bone=%s anchor=%s (source hand-relative ref -> target hand-relative ref)", Text(socket_name).c_str(), Text(old_bone).c_str(), grip->hand);
    }
    Call bone_index(probe, "GetBoneIndex");
    if (!bone_index.Set("BoneName", bone).Invoke() || bone_index.Get<int32_t>("ReturnValue") < 0) return false;
    Call spawn(ue::Default("/Script/Engine.Default__GameplayStatics"), "SpawnObject");
    Object* socket = spawn.Set("ObjectClass", Class("/Script/Engine.SkeletalMeshSocket")).Set("Outer", target_mesh).Invoke() ? spawn.Get<Object*>("ReturnValue") : nullptr;
    if (!socket || !Put(socket, "SocketName", socket_name) || !Put(socket, "BoneName", bone)) return false;
    Call split(Math(), "BreakTransform");
    if (!split.Set("InTransform", local).Invoke() || !Put(socket, "RelativeLocation", split.Get<Vector>("Location")) ||
        !Put(socket, "RelativeRotation", split.Get<Vector>("Rotation")) || !Put(socket, "RelativeScale", split.Get<Vector>("Scale")) || !PutBool(socket, "bForceAlwaysAnimated", true)) return false;
    uintptr_t field = 0; Array* array = nullptr;
    // The Shipping map inserts mesh sockets before skeleton sockets and only
    // falls back to a bone when no socket was found. A same-name mesh socket
    // therefore corrects native direct WEAPON_R as well as *_Socket users,
    // without rewriting any weapon's dynamic attachment or relative transform.
    Object* owner = grip || raw_alias ? target_mesh : target_skeleton;
    if (!ArrayField(owner, "Sockets", field, array)) return false;
    for (int32_t i = 0; i < array->num; ++i) {
        Object* prior = static_cast<Object**>(array->data)[i];
        if (ue::Alive(prior) && Property<Name>(prior, "SocketName") == socket_name) return Fail(L"目标网格已有同名自定义武器 Socket，未覆盖外部编辑");
    }
    const int32_t before = array->num;
    sockets_.reserve(sockets_.size() + 1);
    const int32_t at = reinterpret_cast<int32_t(__fastcall*)(void*, const void*, const void*)>(ue::Base() + kArrayAddRva)(array, reinterpret_cast<void*>(field), &socket);
    // Record an engine-owned insertion before checking the expected return so
    // an unexpected index cannot leave an untracked live socket behind.
    for (int32_t i = 0; i < array->num && array->num <= array->max && array->num < 65536; ++i)
        if (static_cast<Object**>(array->data)[i] == socket) { sockets_.push_back({Id(owner), Id(socket), Id(target_skeleton), socket_name, bone, i, local}); break; }
    if (at != before || array->num != before + 1 || static_cast<Object**>(array->data)[before] != socket) return false;
    if (grip) {
        SocketAdded& added=sockets_.back(); added.grip=grip; added.source_local=source_local; added.raw_bone_alias=RawGripName(bone);
        if (added.raw_bone_alias.None()) return false;
    }
    Logf("NPC_RETARGET temporary socket %s sourceBone=%s targetBone=%s owner=%s", Text(socket_name).c_str(), Text(old_bone).c_str(), Text(bone).c_str(), grip || raw_alias ? "target-mesh grip override" : "target-skeleton missing-bone fallback"); return true;
}
[[maybe_unused]] bool CopySockets(Object* old_mesh, Object* target_mesh, Object* probe, const Profile& profile) {
    Object* source_skeleton = Property<Object*>(old_mesh, "Skeleton"), *target_skeleton = Property<Object*>(target_mesh, "Skeleton");
    if (source_skeleton != ue::FindObject(kSourceSkeleton)) return Fail(L"当前主角不是已审计的 Hun 源骨架");
    std::vector<Object*> sources; std::vector<Name> seen;
    for (Object* owner : {old_mesh, source_skeleton}) {
        uintptr_t field = 0; Array* array = nullptr;
        if (!ArrayField(owner, "Sockets", field, array)) return Fail(L"源网格 / Skeleton Socket 数组布局不符");
        for (int32_t i = 0; i < array->num; ++i) {
            Object* socket = static_cast<Object**>(array->data)[i];
            if (!ue::Alive(socket)) continue;
            const Name name = Property<Name>(socket, "SocketName");
            if (name.None() || std::any_of(seen.begin(), seen.end(), [&](Name prior) { return prior == name; })) continue;
            seen.push_back(name);
            if (Grip(profile, Property<Name>(socket, "BoneName")) || !SocketExists(probe, name)) sources.push_back(socket);
        }
    }
    if (!sources.empty() && !kRebuildSocketMapRva) return Fail(L"尚未验证 Shipping SocketMap 刷新入口，未修改主网格或武器挂点");
    for (Object* socket : sources) {
        Call get(socket, "GetSocketLocalTransform");
        if (!get.Invoke() || !AddSocket(Property<Name>(socket, "SocketName"), Property<Name>(socket, "BoneName"), get.Get<Transform>("ReturnValue"), target_mesh, target_skeleton, probe, profile)) return Fail(L"无法安全构建临时武器 Socket");
    }
    for (size_t i = 0; i < profile.grip_count; ++i) {
        const char* text = profile.grips[i].bone; const std::wstring wide(text, text + std::strlen(text));
        const Name bone = ue::MakeName(wide.c_str());
        if (bone.None() || !AddSocket(bone, bone, Transform{}, target_mesh, target_skeleton, probe, profile)) return Fail(L"无法构建直接武器骨挂接的受管网格 Socket");
        const Name alias=RawGripName(bone);
        if (alias.None() || !AddSocket(alias,bone,Transform{},target_mesh,target_skeleton,probe,profile,true)) return Fail(L"无法构建当前姿势武器骨的独立查询挂点");
        const char* hand_text=profile.grips[i].hand;
        const std::wstring hand_wide(hand_text,hand_text+std::strlen(hand_text)); const Name hand=ue::MakeName(hand_wide.c_str());
        const GripRemapBasis* basis=nullptr;
        for (const GripRemapBasis& candidate:kGripRemapBasis)
            if (Lower(candidate.profile)==Lower(profile.id) && ue::NameIs(bone,candidate.bone)) { basis=&candidate;break; }
        if (!basis) return Fail(L"武器手掌适配的原生参考资料缺失");
        const std::wstring palm_wide(basis->palm,basis->palm+std::strlen(basis->palm));const Name palm=ue::MakeName(palm_wide.c_str());
        for (Name anchor:{hand,palm}) {
            const Name query=RawGripName(anchor);
            if (anchor.None() || query.None() || !AddSocket(query,anchor,Transform{},target_mesh,target_skeleton,probe,profile,true)) return Fail(L"无法构建当前手掌姿势的独立查询挂点");
        }
    }
    // Some configurations name a bone directly rather than its *_Socket.
    // In particular weapon_r_child and Pt_LongBladeScabbard_Back are missing
    // in the NPC meshes. The list comes only from native typed display fields;
    // footstep/trail names in a package's NameMap are not attachment evidence.
    for (const char* route:kWeaponDirectAttachmentBones) {
        const std::wstring wide(route,route+std::strlen(route));const Name name=ue::MakeName(wide.c_str());
        const bool added=std::any_of(sockets_.begin(),sockets_.end(),[&](const SocketAdded& socket){return socket.socket_name==name;});
        if (name.None() || (!added && !SocketExists(probe,name) && !AddSocket(name,name,Transform{},target_mesh,target_skeleton,probe,profile)))
            return Fail(L"武器配置使用的直接骨挂点未能完成原生适配");
    }
    if (!sockets_.empty()) {
        Object* mesh_class = Class("/Script/Engine.SkeletalMesh");
        ue::ForEachObject([&](Object* mesh) {
            if (!ue::IsTemplate(mesh) && ue::Alive(mesh) && ue::IsA(mesh, mesh_class) && Property<Object*>(mesh, "Skeleton") == target_skeleton) {
                if (RebuildSockets(mesh)) socket_maps_.push_back(Id(mesh));
            }
            return true;
        });
        if (std::none_of(socket_maps_.begin(), socket_maps_.end(), [&](Identity identity) { return identity.object == target_mesh; })) return false;
    }
    for (const SocketAdded& added : sockets_) {
        if (!SocketExists(probe, added.socket_name)) return Fail(L"临时 Socket 未通过原生查找验证");
        Call get(probe, "GetSocketTransform");
        if (!get.Set("InSocketName", added.socket_name).Set("TransformSpace", uint8_t{3}).Invoke()) return Fail(L"原生 Socket 局部空间读回失败");
        const Transform actual = get.Get<Transform>("ReturnValue"); double dot = 0;
        for (size_t i = 0; i < 4; ++i) dot += actual.rotation[i] * added.local.rotation[i];
        if (std::abs(std::abs(dot) - 1) > 1e-5) return Fail(L"Socket 查询仍使用原骨/原挂点，武器旋转补偿未生效");
        for (size_t i = 0; i < 3; ++i)
            if (std::abs(actual.translation[i] - added.local.translation[i]) > 0.01 || std::abs(actual.scale[i] - added.local.scale[i]) > 1e-5) return Fail(L"Socket 原生读回未匹配本方临时挂点变换");
        Logf("NPC_RETARGET_SOCKET_READBACK name=%s bone=%s owner=%s T=(%.6f,%.6f,%.6f) Q=(%.8f,%.8f,%.8f,%.8f) verified=1", Text(added.socket_name).c_str(), Text(added.bone_name).c_str(), Text(added.owner.object).c_str(), actual.translation[0], actual.translation[1], actual.translation[2], actual.rotation[0], actual.rotation[1], actual.rotation[2], actual.rotation[3]);
    }
    return true;
}
bool RestoreSockets() {
    bool complete = true;
    std::vector<Object*> skeletons;
    for (const SocketAdded& added : sockets_) if (Same(added.skeleton) && std::find(skeletons.begin(), skeletons.end(), added.skeleton.object) == skeletons.end()) skeletons.push_back(added.skeleton.object);
    for (size_t i = sockets_.size(); i-- > 0;) {
        const SocketAdded& added = sockets_[i];
        if (!Same(added.owner)) { sockets_.erase(sockets_.begin() + i); continue; }
        uintptr_t field = 0; Array* array = nullptr;
        if (!Same(added.socket) || !ArrayField(added.owner.object, "Sockets", field, array) || array->num != added.index + 1 ||
            static_cast<Object**>(array->data)[added.index] != added.socket.object || !(Property<Name>(added.socket.object, "SocketName") == added.socket_name) ||
            !(Property<Name>(added.socket.object, "BoneName") == added.bone_name)) {
            Logf("NPC_RETARGET socket list changed externally; owned-tail removal skipped"); complete = false; continue;
        }
        Call local(added.socket.object, "GetSocketLocalTransform"); bool animated = false;
        if (!local.Invoke() || !TransformNear(added.local, local.Get<Transform>("ReturnValue")) ||
            !Bool(added.socket.object, "bForceAlwaysAnimated", animated) || !animated) {
            Logf("NPC_RETARGET socket local transform/flag changed externally; retained owned socket ledger"); complete = false; continue;
        }
        reinterpret_cast<void(__fastcall*)(void*, const void*, int32_t)>(ue::Base() + kArrayRemoveRva)(array, reinterpret_cast<void*>(field), added.index);
        if (array->num == added.index) sockets_.erase(sockets_.begin() + i);
        else { Logf("NPC_RETARGET engine socket removal did not change expected Num; retained ledger"); complete = false; }
    }
    for (Identity mesh : socket_maps_) if (Same(mesh)) RebuildSockets(mesh.object);
    // Assets loaded during the session may have cached our skeleton sockets
    // after the initial scan. Include them when retiring that shared edit.
    if (!skeletons.empty()) ue::ForEachObject([&](Object* mesh) {
        if (!ue::IsTemplate(mesh) && ue::Alive(mesh) && ue::IsA(mesh, Class("/Script/Engine.SkeletalMesh")) &&
            std::find(skeletons.begin(), skeletons.end(), Property<Object*>(mesh, "Skeleton")) != skeletons.end()) RebuildSockets(mesh);
        return true;
    });
    if (sockets_.empty()) socket_maps_.clear();
    return complete;
}

#pragma warning(push)
#pragma warning(disable: 4324) // Contains a native 16-byte-aligned FTransform parameter.
struct Part {
    Object* source = nullptr; Object* created = nullptr; std::string variable, parent; Name socket; Transform relative;
    bool attachment_known = false, finished = false, head_branch = false;
};
#pragma warning(pop)
struct DormantState {Identity component;uint8_t collision=0;bool primitive=false,overlap=false,tick=false,visible=false,hidden=true,shadow=false,hidden_shadow=false,has_shadow=false,fx=false,active=false,cloth=false,simulation=false;};
struct DisplayBundle {
    int row=-1;Identity pawn,source,source_asset,world,donor,target,body,copy_class;
    std::vector<Part> parts;std::vector<Identity> owned,templates;
    std::vector<DormantState> dormant;ULONGLONG used=0;bool ready=false,broken=false;
};
NpcDisplayPool::Store<DisplayBundle> display_pool_;
std::unique_ptr<DisplayBundle> active_bundle_;
bool pool_enabled_=true;
std::map<int,unsigned> pool_attempts_;
bool PoolOwns(Object* object){for(const auto& b:display_pool_.Items())for(const auto& id:b->owned)if(id.object==object&&Same(id))return true;return false;}
bool PoolFreeze(DisplayBundle& bundle);
bool PoolWake(DisplayBundle& bundle);
bool PoolDrop(DisplayBundle& bundle);
bool PoolParkActive();
bool PoolClear();
std::unique_ptr<DisplayBundle> PoolTake(const Context& context,int row,Object* donor,Object* target,std::vector<Part>& parts);
void WarmDisplayPool(const Context& context,const std::vector<int32_t>& wanted);

// A cloth or static mesh part template with nothing to show (no cloth asset / no mesh).
bool EmptyTemplate(Object* source) {
    if (ue::IsA(source, Class("/Script/ChaosClothAssetEngine.ChaosClothComponent"))) {
        if (Property<Object*>(source, "SkinnedAsset")) return false;
        Call get(source, "GetClothAsset");
        return !(get.Invoke() && get.Get<Object*>("ReturnValue"));
    }
    if (ue::IsA(source, Class("/Script/Engine.StaticMeshComponent"))) return !Property<Object*>(source, "StaticMesh");
    return false;
}
std::string Variable(Object* object) {
    std::string value = Lower(Text(object)); const std::string suffix = "_gen_variable";
    if (value.size() > suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) value.resize(value.size() - suffix.size());
    return value;
}
Object* LoadClassPackage(const std::string& package);
Object* LoadDonor(const Profile& profile) {
    return LoadClassPackage(profile.donor);
}
// A blueprint class by package path ("/PBZCombatAsset/.../GA_X" -> GA_X_C): found if loaded, else loaded blocking.
// A copy the collector has marked for purge is not "loaded" (FindObject skips it): the engine's own
// load brings the class back, and only a live class is returned.
Object* LoadClassPackage(const std::string& package) {
    const std::string leaf = package.substr(package.find_last_of('/') + 1) + "_C";
    Object* cls = ue::FindObject((package + "." + leaf).c_str());
    if (!cls) {
        SoftPath path; const std::wstring p(package.begin(), package.end()), a(leaf.begin(), leaf.end());
        path.package = ue::MakeName(p.c_str()); path.asset = ue::MakeName(a.c_str());
        Call load(Kismet(), "LoadClassAsset_Blocking");
        unsigned char* slot = load.Slot("AssetClass", sizeof(path));
        if (!slot) return nullptr;
        std::memcpy(slot, &path, sizeof(path));
        if (load.Invoke()) cls = load.Get<Object*>("ReturnValue");
    }
    return ue::Alive(cls) ? cls : nullptr;
}
bool SourceOwner(Object* object, Object* cls, Object* cdo) {
    for (size_t guard = 0; object && guard < 8; ++guard, object = ue::OuterOf(object)) if (object == cls || object == cdo) return true;
    return false;
}
// How far up the donor's blueprint chain the template's owner is (0 = the donor or its CDO), or -1.
// Boss donors inherit body parts from a parent blueprint; a child's own override is closer and wins.
int32_t SourceDepth(Object* object, Object* cls, Object* cdo) {
    for (size_t guard = 0; object && guard < 8; ++guard, object = ue::OuterOf(object)) {
        if (object == cdo) return 0;
        int32_t depth = 0;
        for (Object* type = cls; type && depth < 8; type = ue::At<Object*>(type, ue::layout::kSuper), ++depth) if (object == type) return depth;
    }
    return -1;
}
// Part templates by donor class and part name (the one closest up the donor's blueprint chain): a look worn again
// takes them from here while each is the same live template; the parts not cached are found in one pass over the
// object array (by FName, not by text), not one pass per part.
struct PartTemplate { Identity object; int32_t depth = 0; };
std::map<std::pair<Object*, std::string>, PartTemplate> part_cache_;
size_t part_scans_ = 0;
// The template of each part of `profile` (nullptr: none) and its depth up the donor's blueprint chain.
void PartTemplates(Object* cls, Object* cdo, const Profile& profile, std::vector<Object*>& templates, std::vector<int32_t>& depths) {
    const size_t count = profile.part_count;
    templates.assign(count, nullptr); depths.assign(count, std::numeric_limits<int32_t>::max());
    std::vector<size_t> missing; std::vector<Name> names;
    for (size_t i = 0; i < count; ++i) {
        const auto cached = part_cache_.find({cls, profile.parts[i]});
        if (cached != part_cache_.end()) {
            Object* object = cached->second.object.object;
            if (Same(cached->second.object) && ue::IsTemplate(object) && ue::NameIs(ue::NameOf(object), profile.parts[i]) &&
                SourceDepth(object, cls, cdo) == cached->second.depth) { templates[i] = object; depths[i] = cached->second.depth; continue; }
            part_cache_.erase(cached);
        }
        const std::wstring wide(profile.parts[i], profile.parts[i] + std::strlen(profile.parts[i]));
        missing.push_back(i); names.push_back(ue::MakeName(wide.c_str()));
    }
    if (!missing.empty()) {
        ++part_scans_; size_t open = missing.size();  // parts without a template on the donor itself (depth 0) yet
        ue::ForEachObject([&](Object* object) {
            if (!ue::IsTemplate(object)) return true;
            const Name name = ue::NameOf(object);
            for (size_t k = 0; k < missing.size(); ++k) {
                const size_t i = missing[k];
                if (depths[i] == 0 || !(names[k].None() ? ue::NameIs(name, profile.parts[i]) : name == names[k])) continue;
                if (!ue::Alive(object)) break;
                const int32_t depth = SourceDepth(object, cls, cdo);
                if (depth >= 0 && depth < depths[i]) { templates[i] = object; depths[i] = depth; if (depth == 0 && --open == 0) return false; }
            }
            return true;
        });
        if (part_cache_.size() > 1024) part_cache_.clear();
        for (const size_t i : missing) if (templates[i]) part_cache_[{cls, profile.parts[i]}] = PartTemplate{Id(templates[i]), depths[i]};
    }
}
bool LoadParts(Object* cls, Object* cdo, const Profile& profile, std::vector<Part>& parts) {
    const size_t count = profile.part_count;
    std::vector<Object*> templates; std::vector<int32_t> depths;
    PartTemplates(cls, cdo, profile, templates, depths);
    for (size_t i = 0; i < count; ++i) {
        Object* found = templates[i]; const int32_t best = depths[i];
        if (found && best > 0) Logf("NPC_RETARGET part=%s inherited from blueprint parent depth=%d", profile.parts[i], best);
        if (!found || !ue::IsA(found, Class("/Script/Engine.SceneComponent"))) return Fail(L"NPC 部件模板缺失或不是场景组件");
        Part part; part.source = found; part.variable = Variable(found);
        if (!TransformOf(found, part.relative)) return Fail(L"无法读取 NPC 部件的原始相对变换");
        parts.push_back(part);
    }
    // Read the cooked SCS graph, including socket names and parent-node links;
    // template AttachParent alone is not populated for every SCS component.
    std::map<Object*, std::string> variables;
    std::vector<Object*> nodes;
    for (Object* type = cls; type; type = ue::At<Object*>(type, ue::layout::kSuper)) {
        Object* scs = Property<Object*>(type, "SimpleConstructionScript");
        const Array all = Property<Array>(scs, "AllNodes");
        if (all.num < 0 || all.num > 1024 || (all.num && !all.data)) return false;
        for (int32_t i = 0; i < all.num; ++i) {
            Object* node = static_cast<Object**>(all.data)[i]; if (!ue::Alive(node)) continue;
            variables[node] = Lower(Text(Property<Name>(node, "InternalVariableName"))); nodes.push_back(node);
        }
    }
    for (Object* node : nodes) {
        const std::string variable = variables[node];
        auto part = std::find_if(parts.begin(), parts.end(), [&](const Part& value) { return value.variable == variable; });
        if (part != parts.end()) {
            const Name parent = Property<Name>(node, "ParentComponentOrVariableName");
            part->parent = parent.None() ? "" : Lower(Text(parent)); part->socket = Property<Name>(node, "AttachToName");
            part->attachment_known = true;
        }
    }
    // AllNodes is parent-before-child in both exact donor BPs. Do not let a
    // later child's empty external-parent field erase its real SCS edge.
    // Internal links are authoritative and applied only after all own records.
    for (Object* node : nodes) {
        const std::string variable = variables[node];
        const Array children = Property<Array>(node, "ChildNodes");
        if (children.num < 0 || children.num > 1024 || (children.num && !children.data)) return false;
        for (int32_t i = 0; i < children.num; ++i) {
            const auto child = variables.find(static_cast<Object**>(children.data)[i]);
            if (child == variables.end()) continue;
            for (Part& value : parts) if (value.variable == child->second) { value.parent = variable; value.attachment_known = true; }
        }
    }
    Object* source_primary = Property<Object*>(cdo, "Mesh"), *source_root = Property<Object*>(cdo, "RootComponent");
    for (Part& part : parts) if (!part.attachment_known) {
        Object* parent = Property<Object*>(part.source, "AttachParent");
        if (parent && parent == source_primary) { part.parent = "mesh"; part.attachment_known = true; }
        else if (parent && parent == source_root) { part.parent = "rootcomponent"; part.attachment_known = true; }
        else if (parent) {
            const auto found = std::find_if(parts.begin(), parts.end(), [&](const Part& value) { return value.source == parent; });
            if (found != parts.end()) { part.parent = found->variable; part.attachment_known = true; }
        }
        if (!part.attachment_known && ue::IsA(part.source, Class("/Script/HairStrandsCore.GroomComponent"))) {
            Object* binding = Property<Object*>(part.source, "BindingAsset"), *target = Property<Object*>(binding, "TargetSkeletalMesh");
            if (target && SkelMesh(source_primary) == target) { part.parent = "mesh"; part.attachment_known = true; }
            else for (const Part& candidate : parts) if (target && SkelMesh(candidate.source) == target) {
                part.parent = candidate.variable; part.attachment_known = true; break;
            }
        }
        if (!part.attachment_known) {
            Logf("NPC_RETARGET unverified attachment graph part=%s", part.variable.c_str());
            return Fail(L"NPC 部件缺少经过核定的 SCS 挂接信息");
        }
    }
    // A part whose template holds no asset (a ChaosCloth component without a cloth asset, a static mesh component without
    // a mesh: 荆野's CA_JingYe_Hair01) shows nothing on the boss either; it is left out instead of refusing the whole look.
    // A part attached to it hangs from its parent instead.
    for (size_t i = 0; i < parts.size();) {
        if (!EmptyTemplate(parts[i].source)) { ++i; continue; }
        const Part gone = parts[i];
        for (Part& child : parts) if (child.parent == gone.variable) child.parent = gone.parent;
        Logf("NPC_RETARGET part=%s (%s) left out: its template holds no asset", gone.variable.c_str(), Text(ue::ClassOf(gone.source)).c_str());
        parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return true;
}
bool ValidateSourceFace(const Profile& profile, const std::vector<Part>& parts) {
    if (!profile.head) return true; // Single-mesh NPC: no separate RigLogic head to isolate.
    const auto head = std::find_if(parts.begin(), parts.end(), [](const Part& part) { return part.variable == "meshhead"; });
    if (head == parts.end()) return Fail(L"原 NPC 头部组件缺失");
    Object* mesh = SkelMesh(head->source), *post_class = EffectivePostClass(head->source);
    AnimOptions options;
    Object* copy_pose = Class("/HuaShira/Characters/Hero/Hero_Hun/Hun_CopyPose.Hun_CopyPose_C");
    if (!mesh || mesh != ue::FindObject(profile.head) || !post_class || post_class != ue::FindObject(profile.head_postprocess) ||
        !ReadAnimOptions(head->source, options) || options.disabled || !copy_pose || AnimClass(head->source) != copy_pose)
        return Fail(L"NPC Head 的原网格 / CopyPose / 独立 PostProcess 与精确包资料不一致");
    Object* dna_class = Class("/Script/RigLogicModule.DNAAsset"), *dna = nullptr;
    if (Offset(mesh, "AssetUserData") < 0 || !dna_class) return Fail(L"当前运行时缺少 Head AssetUserData / DNA 元数据");
    const Array user_data = Property<Array>(mesh, "AssetUserData");
    if (user_data.num < 0 || user_data.num > 128 || user_data.max < user_data.num || (user_data.num && !user_data.data)) return false;
    for (int32_t i = 0; i < user_data.num; ++i) {
        Object* data = static_cast<Object**>(user_data.data)[i];
        if (ue::Alive(data) && ue::IsA(data, dna_class) && ue::OuterOf(data) == mesh) dna = data;
    }
    Logf("NPC_RETARGET_FACE_SOURCE id=%s head=%s animClass=%s postClass=%s DNA=%s DNAOuter=%s disabled=%d lod=%d",
        profile.id, Text(mesh).c_str(), Text(AnimClass(head->source)).c_str(), Text(post_class).c_str(), Text(dna).c_str(),
        Text(dna ? ue::OuterOf(dna) : nullptr).c_str(), options.disabled ? 1 : 0, options.lod);
    return dna != nullptr || Fail(L"NPC 原 Head 没有自身 DNAAsset，未用 Hun 面部替代");
}
void CopyRenderFlags(Object* from, Object* to) {
    for (const char* flag : {"CastShadow", "bCastDynamicShadow", "bCastStaticShadow", "bRenderInMainPass", "bRenderInDepthPass", "bVisibleInRayTracing", "bReceivesDecals", "bUseAttachParentBound"}) {
        bool value = false; if (Bool(from, flag, value)) PutBool(to, flag, value);
    }
}
bool LightingChannels(Object* component, std::array<bool,3>& channels) {
    const uintptr_t field=component?Field(ue::ClassOf(component),"LightingChannels"):0;
    Object* type=Class("/Script/Engine.LightingChannels");
    if (!field || !type || ue::At<Object*>(reinterpret_cast<void*>(field),0x70)!=type ||
        ue::At<int32_t>(reinterpret_cast<void*>(field),ue::layout::kPropElementSize)!=1 || ue::StructSize(type)!=1) return false;
    Object* data=component+ue::At<int32_t>(reinterpret_cast<void*>(field),ue::layout::kPropOffset);
    const char* names[]={"bChannel0","bChannel1","bChannel2"};
    for (size_t i=0;i<channels.size();++i) {
        const uintptr_t member=Field(type,names[i]);
        if (!member || ue::At<uint8_t>(reinterpret_cast<void*>(member),0x70)!=1 ||
            ue::At<int32_t>(reinterpret_cast<void*>(member),ue::layout::kPropOffset)!=0 || ue::At<uint8_t>(reinterpret_cast<void*>(member),0x71)!=0) return false;
        const uint8_t mask=ue::At<uint8_t>(reinterpret_cast<void*>(member),0x73);
        if (mask!=(1u<<i)) return false;
        channels[i]=(data[0]&mask)!=0;
    }
    return true;
}
bool MatchPlayerLighting(Object* player_mesh, Object* component) {
    std::array<bool,3> expected{},actual{};
    if (!LightingChannels(player_mesh,expected)||!LightingChannels(component,actual)) return false;
    if (expected==actual) return true;
    // PBZ's player body/head use channels 0+1; new Engine components default
    // to channel 0. Copying only CastShadow made replacement heads miss the
    // player-only fill lights. Use the setter so registered render proxies are
    // dirtied too; never change scene lights, exposure or skin materials.
    if (!Call(component,"SetLightingChannels").Set("bChannel0",expected[0]).Set("bChannel1",expected[1]).Set("bChannel2",expected[2]).Invoke() ||
        !LightingChannels(component,actual) || expected!=actual) return false;
    Logf("NPC_LIGHT channels component=%s player=(%d,%d,%d) native-readback=1",Text(component).c_str(),actual[0]?1:0,actual[1]?1:0,actual[2]?1:0);
    return true;
}
bool RefreshOwnedLighting() {
    if (!Same(primary_)) return false;
    Object* primitive=Class("/Script/Engine.PrimitiveComponent"); if (!primitive) return false;
    for (const Identity& part:added_) if (Same(part) && ue::IsA(part.object,primitive) && !MatchPlayerLighting(primary_.object,part.object)) return false;
    return true;
}
Object* BindingParent(Object* binding, Object* primary, const std::vector<Part>& parts) {
    Object* target = Property<Object*>(binding, "TargetSkeletalMesh");
    if (target && SkelMesh(primary) == target) return primary;
    for (const Part& part : parts) if (part.created && target && SkelMesh(part.created) == target) return part.created;
    return nullptr;
}
// Cutscene donors can ship cloth templates disabled or suspended (the sequence drives them).
// A worn part always simulates: enable, resume, and start from the attached pose.
void EnableCloth(const Part& part) {
    bool template_enabled = true, template_suspended = false;
    const bool read = Bool(part.source, "bEnableSimulation", template_enabled) && Bool(part.source, "bSuspendSimulation", template_suspended);
    const bool set = Call(part.created, "SetEnableSimulation").Set("bEnable", true).Invoke();
    Call suspended(part.created, "IsSimulationSuspended");
    const bool was_suspended = suspended.Invoke() && suspended.Get<bool>("ReturnValue");
    const bool resumed = !was_suspended || Call(part.created, "ResumeSimulation").Invoke();
    const bool reset = Call(part.created, "ForceNextUpdateTeleportAndReset").Invoke();
    Call enabled(part.created, "IsSimulationEnabled");
    const bool simulating = enabled.Invoke() && enabled.Get<bool>("ReturnValue");
    Logf("NPC_CLOTH part=%s template(read=%d enabled=%d suspended=%d) set=%d wasSuspended=%d resumed=%d reset=%d simulating=%d",
        part.variable.c_str(), read ? 1 : 0, template_enabled ? 1 : 0, template_suspended ? 1 : 0, set ? 1 : 0, was_suspended ? 1 : 0, resumed ? 1 : 0, reset ? 1 : 0, simulating ? 1 : 0);
}
// The source line of Configure's last refusal (0: none): the part assembly log names the step that refused.
int configure_line_ = 0;
bool ConfigureFailed(int line) { configure_line_ = line; return false; }
bool Configure(Object* source, Object* dest, Object* primary, Object* parent, const std::vector<Part>& parts) {
    const bool groom = ue::IsA(source, Class("/Script/HairStrandsCore.GroomComponent"));
    if (groom) {
        Object* binding = Property<Object*>(source, "BindingAsset");
        Object* binding_parent = BindingParent(binding, primary, parts);
        if (!binding_parent || binding_parent != parent) return Fail(L"Groom 原挂接父组件与绑定目标不同，未更换父坐标系或套用错误相对变换");
        for (const char* property : {"GroomAsset", "BindingAsset", "PhysicsAsset"}) if (!Put(dest, property, Property<Object*>(source, property))) return ConfigureFailed(__LINE__);
        const int32_t settings = Offset(source, "SimulationSettings"), out_settings = Offset(dest, "SimulationSettings");
        Object* settings_class = Class("/Script/HairStrandsCore.HairSimulationSettings"), *setup_class = Class("/Script/HairStrandsCore.HairSimulationSetup");
        if (settings < 0 || out_settings < 0 || ue::StructSize(settings_class) != 0x90 ||
            ue::Offset(settings_class, "SimulationSetup") != 8 || ue::Offset(setup_class, "LocalBone") != 16) return ConfigureFailed(__LINE__);
        const ue::String local_bone = ue::At<ue::String>(source, settings + 0x18);
        if (local_bone.num < 0 || local_bone.num > 4096 || local_bone.max < local_bone.num || (local_bone.num && !local_bone.data)) return ConfigureFailed(__LINE__);
        ue::String owned_bone;
        if (local_bone.num > 1) {
            // The reflected string library returns a separate UE-owned FString.
            // Borrow A/B only for the call; transfer just ReturnValue to the new
            // component so template and clone never share a destructed buffer.
            Call clone(ue::Default("/Script/Engine.Default__KismetStringLibrary"), "Concat_StrStr");
            if (!clone.Set("A", local_bone).Set("B", ue::String{}).Invoke()) return ConfigureFailed(__LINE__);
            owned_bone = clone.Get<ue::String>("ReturnValue");
            if (owned_bone.num != local_bone.num || !owned_bone.data || owned_bone.data == local_bone.data) {
                if (owned_bone.data && owned_bone.data != local_bone.data) ue::Free(const_cast<wchar_t*>(owned_bone.data));
                return ConfigureFailed(__LINE__);
            }
        }
        ue::Free(const_cast<wchar_t*>(ue::At<ue::String>(dest, out_settings + 0x18).data));
        std::memcpy(dest + out_settings, source + settings, 0x90);
        std::memcpy(dest + out_settings + 0x18, &owned_bone, sizeof(owned_bone));
        for (const char* property : {"bUseCards", "bForceLocalSpaceSimulation"}) { bool value = false; if (Bool(source, property, value)) PutBool(dest, property, value); }
        if (!PutBool(dest, "bIsMainHair", true)) return ConfigureFailed(__LINE__);
    } else if (ue::IsA(source, Class("/Script/ChaosClothAssetEngine.ChaosClothComponent"))) {
        Object* asset = Property<Object*>(source, "SkinnedAsset");
        if (!asset) { Call get(source, "GetClothAsset"); if (get.Invoke()) asset = get.Get<Object*>("ReturnValue"); }
        if (!asset) return ConfigureFailed(__LINE__);
        Call set(dest, "SetClothAsset");
        if (!set.Set("InClothAsset", asset).Invoke()) return ConfigureFailed(__LINE__);
    } else if (ue::IsA(source, Class("/Script/Engine.SkeletalMeshComponent"))) {
        Object* asset = SkelMesh(source);
        if (!asset) return true;
        AnimOptions options;
        if (!ReadAnimOptions(source, options) || !WriteAnimOptions(dest, options) || !SetMesh(dest, asset)) return ConfigureFailed(__LINE__);
        Object* own_class = AnimClass(source);
        if (own_class && !Call(dest, "SetAnimInstanceClass").Set("NewClass", own_class).Invoke()) return ConfigureFailed(__LINE__);
    } else if (ue::IsA(source, Class("/Script/Engine.StaticMeshComponent"))) {
        Object* asset = Property<Object*>(source, "StaticMesh");
        if (!asset || !Call(dest, "SetStaticMesh").Set("NewMesh", asset).Invoke()) return ConfigureFailed(__LINE__);
    } else return ConfigureFailed(__LINE__);
    if (!CopyMaterials(source, dest)) return ConfigureFailed(__LINE__);
    CopyRenderFlags(source, dest);
    if (groom) {
        Call scale(dest, "SetBoundsScale");
        if (PutBool(dest, "bUseAttachParentBound", false) && scale.Set("NewBoundsScale", kHairBoundsScale).Invoke())
            Logf("NPC_RETARGET hair bounds component=%s scale=%.1f ownBounds=1", Text(dest).c_str(), kHairBoundsScale);
        else Logf("NPC_RETARGET hair bounds unchanged component=%s (setter unavailable)", Text(dest).c_str());
    }
    if (!MatchPlayerLighting(primary,dest)) return Fail(L"NPC 部件未能同步主角的原生照明通道，已请求恢复");
    for (const char* property : {"bAbsoluteLocation", "bAbsoluteRotation", "bAbsoluteScale"}) {
        bool absolute = false;
        if (!Bool(source, property, absolute) || !PutBool(dest, property, absolute)) return ConfigureFailed(__LINE__);
    }
    if (!Call(dest, "SetCollisionEnabled").Set("NewType", uint8_t{0}).Invoke() ||
        !Call(dest, "SetGenerateOverlapEvents").Set("bInGenerateOverlapEvents", false).Invoke()) return ConfigureFailed(__LINE__);
    if (!groom && ue::IsA(dest, Class("/Script/Engine.SkinnedMeshComponent"))) {
        const bool owns_animation = ue::IsA(source, Class("/Script/Engine.SkeletalMeshComponent")) && AnimClass(source);
        Object* pose_parent = parent;
        for (size_t guard = 0; pose_parent && guard < 32 && !ue::IsA(pose_parent, Class("/Script/Engine.SkinnedMeshComponent")); ++guard)
            pose_parent = Property<Object*>(pose_parent, "AttachParent");
        if (!pose_parent) pose_parent = primary;
        // Authored CopyPose components must run their own graph and Head's
        // RigLogic post-process. LeaderPose bypasses that identity chain.
        Object* leader = owns_animation ? nullptr : pose_parent;
        if (!Call(dest, "SetLeaderPoseComponent").Set("NewLeaderBoneComponent", leader).Set("bForceUpdate", true).Set("bInFollowerShouldTickPose", false).Invoke() ||
            !Call(dest, "AddTickPrerequisiteComponent").Set("PrerequisiteComponent", pose_parent).Invoke()) return ConfigureFailed(__LINE__);
        bool influenced = false;
        if (Bool(source, "bInfluencedByMainHairMode", influenced)) PutBool(dest, "bInfluencedByMainHairMode", influenced);
    }
    return true;
}
bool Assemble(const Context& context, Object* source_primary, const Profile& profile, std::vector<Part>& parts,DisplayBundle* warming=nullptr) {
    Transform body_relative; if (!TransformOf(source_primary, body_relative)) return false;
    size_t remaining=static_cast<size_t>(std::count_if(parts.begin(),parts.end(),[](const Part& part){return !part.finished;}));
    for (size_t pass = 0; remaining && pass <= parts.size(); ++pass) {
        size_t progress = 0;
        for (Part& part : parts) {
            if (part.finished) continue;
            // Only a RigLogic head gets the hidden-until-isolated face path; a plain head mesh is an ordinary part.
            Object* parent = context.mesh; Transform relative = part.relative; part.head_branch=part.variable=="meshhead"&&profile.head;
            if (!part.parent.empty() && part.parent != "mesh" && part.parent != "charactermesh0") {
                const auto found = std::find_if(parts.begin(), parts.end(), [&](const Part& candidate) { return candidate.variable == part.parent; });
                if (found != parts.end()) { if (!found->finished) continue; parent = found->created; part.head_branch=part.head_branch||found->head_branch; }
                else if (part.parent == "capsulecomponent" || part.parent == "rootcomponent" || part.parent == "defaultsceneroot") {
                    if (!Relative(part.relative, body_relative, relative)) return false;
                } else return Fail(L"NPC 部件的父组件未被完整解析");
            } else if (part.parent.empty() && !Relative(part.relative, body_relative, relative)) return false;
            const bool groom = ue::IsA(part.source, Class("/Script/HairStrandsCore.GroomComponent"));
            if (groom && !BindingParent(Property<Object*>(part.source, "BindingAsset"), context.mesh, parts)) continue;
            const bool reused=part.created!=nullptr;
            if(!reused){part.created=Add(context.pawn,ue::ClassOf(part.source),relative);if(!part.created)return false;
                (warming?warming->owned:added_).push_back(Id(part.created));}
            if(warming&&!SetVisible(part.created,false,true))return false;
            if (part.head_branch && !SetVisible(part.created,false,true)) return false;
            configure_line_ = 0;
            if (!reused&&!Configure(part.source, part.created, context.mesh, parent, parts)) {
                Logf("NPC_RETARGET part %s (%s) refused by Configure at npc_retarget.cpp:%d parent=%s",
                    part.variable.c_str(), Text(ue::ClassOf(part.source)).c_str(), configure_line_, Text(parent).c_str());
                return Fail(L"NPC 部件的原生装配接口未能完成");
            }
            if (part.head_branch && ue::IsA(part.created,Class("/Script/Engine.SkeletalMeshComponent")) &&
                (!Put(part.created,"VisibilityBasedAnimTickOption",uint8_t{0}) || !PutBool(part.created,"bEnableUpdateRateOptimizations",false)))
                return Fail(L"Head 首次隐藏评估所需的动画 tick 设置未能验证");
            if(reused){if(!ue::Alive(part.created)||Property<Object*>(part.created,"AttachParent")!=parent||!(Property<Name>(part.created,"AttachSocketName")==part.socket))return false;}
            else if(!AttachFinish(context.pawn,part.created,parent,part.socket,relative))return false;
            if(warming){part.finished=true;return PoolFreeze(*warming);} // one registered part per idle upkeep

            if (ue::IsA(part.created, Class("/Script/ChaosClothAssetEngine.ChaosClothComponent"))) EnableCloth(part);
            if (part.variable=="meshhead" && profile.head && !InstallFaceHook(part.created,profile)) return Fail(L"自有 Head 的面部 CopyPose 隔离未能通过原生布局 / 私有映射校验");
            if (ue::IsA(part.source, Class("/Script/Engine.SkeletalMeshComponent")) && SkelMesh(part.source)) {
                AnimOptions options;
                Object* source_class = AnimClass(part.source), *instance = AnimInstance(part.created);
                AnimOptions actual_options;
                if (!ReadAnimOptions(part.source, options) || !ReadAnimOptions(part.created, actual_options) || !SameAnimOptions(options, actual_options) ||
                    (source_class && (!VerifyPostProcess(part.created, options, part.variable.c_str()) || AnimClass(part.created) != source_class ||
                        !ue::Alive(instance) || ue::ClassOf(instance) != source_class || !HasNoLeader(part.created))))
                    return Fail(L"NPC 部件原生动画 / Head PostProcess 实例读回失败，已请求恢复");
                Logf("NPC_RETARGET_ANIM part=%s ownClass=%s ownInstance=%s leaderCleared=%d sourceParent=%s",
                    part.variable.c_str(), Text(AnimClass(part.created)).c_str(), Text(instance).c_str(), HasNoLeader(part.created) ? 1 : 0, Text(parent).c_str());
            }
            Logf("NPC_RETARGET_FRAME part=%s parent=%s socket=%s T=(%.6f,%.6f,%.6f) Q=(%.8f,%.8f,%.8f,%.8f) S=(%.6f,%.6f,%.6f) native-readback=1",
                part.variable.c_str(), Text(parent).c_str(), Text(part.socket).c_str(), relative.translation[0], relative.translation[1], relative.translation[2],
                relative.rotation[0], relative.rotation[1], relative.rotation[2], relative.rotation[3], relative.scale[0], relative.scale[1], relative.scale[2]);
            bool visible = true, hidden = false;
            if (!VisibilityOf(part.source, visible, hidden)) return false;
            if (part.head_branch) face_visibility_.push_back({Id(part.created),visible,hidden});
            else if (!SetVisible(part.created, visible, hidden)) return false;
            if (!visible || hidden) authored_.push_back({Id(part.created),visible,hidden});
            if (!part.head_branch && visible && !hidden) {
                if (groom) reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base() + ue::layout::kApplyGroomHairMode)(part.created);
                else if (ue::IsA(part.created, Class("/Script/Engine.SkinnedMeshComponent")))
                    reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base() + ue::layout::kApplySkinnedHairMode)(part.created);
            }
            Logf("NPC_RETARGET part=%s class=%s parent=%s visible=%d hidden=%d", part.variable.c_str(), Text(ue::ClassOf(part.created)).c_str(), Text(parent).c_str(), visible ? 1 : 0, hidden ? 1 : 0);
            part.finished = true; --remaining; ++progress;
        }
        if (!progress && remaining) return Fail(L"NPC 部件图存在未解决的依赖，未留下半套模型");
    }
    return remaining == 0;
}
// UI widget components are mesh components too: the lock-on indicator (TargetingComponent +0x1c0, made at
// BeginPlay on the hero and shown with SetVisibility only) must keep bHiddenInGame clear, or no lock is drawn.
bool IsAppearancePart(Object* component) {
    const char* keep[] = {"weapon", "knife", "knifescab", "sword", "blade", "switchkey", "scabbard", "scabard", "shield"};
    if (ue::NameContains(ue::NameOf(component), keep, std::size(keep))) return false;
    if (Object* widget = Class("/Script/UMG.WidgetComponent"); widget && ue::IsA(component, widget)) return false;
    return ue::IsA(component, Class("/Script/Engine.MeshComponent")) || ue::IsA(component, Class("/Script/HairStrandsCore.GroomComponent"));
}
// The hero's small dagger and its sheath on the back (pawn components Knife / KnifeScab on
// Pt_Knife_Back_Socket / Pt_KnifeScabbard_Back_Socket): part of Hun's look, not a weapon actor.
bool IsBackKnife(Object* component) {
    const std::string name = Lower(Text(component));
    return (name == "knife" || name == "knifescab") && ue::IsA(component, Class("/Script/Engine.MeshComponent"));
}
bool HideOriginalParts(const Context& context) {
    for (Object* component : Components(context.pawn, Class("/Script/Engine.SceneComponent"))) {
        if (component == context.mesh || component == backup_.object || PoolOwns(component) || (!IsAppearancePart(component) && !IsBackKnife(component))) continue;
        bool visible = false, hidden = false;
        if (!VisibilityOf(component, visible, hidden)) return false;
        Visibility saved{Id(component),visible,hidden};
        if(visible&&!hidden&&Bool(component,"CastShadow",saved.cast_shadow)&&Bool(component,"bCastHiddenShadow",saved.hidden_shadow))saved.shadows_owned=true;
        hidden_.push_back(saved);
        if(saved.shadows_owned&&(!PutBool(component,"bCastHiddenShadow",false)||!Call(component,"SetCastShadow").Set("NewCastShadow",false).Invoke()))return false;
        if (!SetVisibleOwned(component,false,true,hidden_.back().visible_written,hidden_.back().hidden_written)) return false;
        if (IsBackKnife(component)) Logf("NPC_RETARGET back dagger hidden component=%s socket=%s wasVisible=%d", Text(component).c_str(), Text(Property<Name>(component, "AttachSocketName")).c_str(), visible && !hidden ? 1 : 0);
    }
    return true;
}
[[maybe_unused]] bool ValidateAttachments(Object* source_primary, Object* target_probe) {
    const Array children = Property<Array>(source_primary, "AttachChildren");
    if (children.num < 0 || children.num > 2048 || (children.num && !children.data)) return false;
    for (int32_t i = 0; i < children.num; ++i) {
        Object* child = static_cast<Object**>(children.data)[i]; if (!ue::Alive(child) || IsAppearancePart(child)) continue;
        const Name socket = Property<Name>(child, "AttachSocketName");
        if (!socket.None() && !SocketExists(target_probe, socket)) {
            wchar_t detail[500]{};
            _snwprintf_s(detail, _countof(detail), _TRUNCATE, L"当前附件 %S 的挂点 %S 尚未适配；未替换主网格", Text(child).c_str(), Text(socket).c_str());
            failure_text_ = detail; Status(L"%s", detail);
            Logf("NPC_RETARGET missing required attachment socket=%s child=%s", Text(socket).c_str(), Text(child).c_str()); return false;
        }
    }
    return true;
}
bool SetCameraLift(Object* camera,double lift) {
    const Name name=ue::MakeName(kCameraAdditive);if(name.None())return false;
    return Call(camera,"SetAdditiveParameter_TargetOffset").Set("AdditiveName",name).Set("TargetOffset",Vector{0,0,lift}).Set("BlendTime",0.4f).Invoke();
}
void ApplyCameraLift(Object* pawn) {
    camera_owned_=false;camera_={};camera_probe_at_=0;
    if(std::abs(camera_lift_)<kCameraLiftMin){Logf("NPC_CAMERA headDeltaCm=%.1f under %.0f; camera unchanged",camera_lift_,kCameraLiftMin);return;}
    const double lift=(std::max)(kCameraLiftLow,(std::min)(camera_lift_,kCameraLiftHigh));
    Object* atpc=Class("/Script/AdvancedThirdPersonCamera.ATPCCameraComponent"),*camera=nullptr;
    if(atpc){Call get(pawn,"GetComponentByClass");if(get.Set("ComponentClass",atpc).Invoke())camera=get.Get<Object*>("ReturnValue");}
    if(!ue::Alive(camera)){Object* nb=Property<Object*>(pawn,"NBCamera");
        Logf("NPC_CAMERA no ATPC camera on the pawn (NBCamera=%s class=%s); height adaptation skipped",Text(nb).c_str(),Text(ue::Alive(nb)?ue::ClassOf(nb):nullptr).c_str());return;}
    if(!SetCameraLift(camera,lift)){Logf("NPC_CAMERA SetAdditiveParameter_TargetOffset refused on %s",Text(camera).c_str());return;}
    camera_=Id(camera);camera_owned_=true;camera_applied_=lift;camera_holds_=0;camera_probe_at_=GetTickCount64()+1500;camera_hold_at_=GetTickCount64()+kCameraHoldMs;
    Logf("NPC_CAMERA raised camera=%s class=%s headDeltaCm=%.1f appliedCm=%.1f additive=%ls",Text(camera).c_str(),Text(ue::ClassOf(camera)).c_str(),camera_lift_,lift,kCameraAdditive);
}
// Same named additive, same value: a no-op while it holds, and back in place if a camera mode reset it.
void HoldCameraLift() {
    if(!camera_owned_||!Same(camera_)||GetTickCount64()<camera_hold_at_)return;camera_hold_at_=GetTickCount64()+kCameraHoldMs;
    if(SetCameraLift(camera_.object,camera_applied_))++camera_holds_;
}
void ResetCameraLift() {
    if(!camera_owned_)return;camera_owned_=false;camera_probe_at_=camera_hold_at_=0;
    Logf("NPC_CAMERA held appliedCm=%.1f reasserts=%u",camera_applied_,camera_holds_);
    if(!Same(camera_)){Logf("NPC_CAMERA camera component gone; nothing to reset");camera_={};return;}
    const bool reset=SetCameraLift(camera_.object,0);Logf("NPC_CAMERA reset camera=%s ok=%d",Text(camera_.object).c_str(),reset?1:0);camera_={};
}
// One readback after the blend, so a log shows whether the camera kept the offset.
void ProbeCameraLift() {
    if(!camera_owned_||!camera_probe_at_||GetTickCount64()<camera_probe_at_||!Same(camera_))return;camera_probe_at_=0;
    Call location(camera_.object,"GetCameraLocationObject");Object* object=location.Invoke()?location.Get<Object*>("ReturnValue"):nullptr;
    Vector offset{};bool read=false;if(ue::Alive(object)){Call get(object,"GetTargetOffset");if(get.Invoke()){offset=get.Get<Vector>("ReturnValue");read=true;}}
    Logf("NPC_CAMERA readback locationObject=%s read=%d targetOffset=(%.1f,%.1f,%.1f)",Text(object).c_str(),read?1:0,offset.x,offset.y,offset.z);
}
// The grab module's counted strong-reference pair (exact bytes checked before every call).
bool ClassRefsReady() { return ue::ReferenceCodeMatches(); }
// Only package-root UClasses: never an actor, component or anything whose outer holds a world.
// The first few refusals per process carry the object's reference state (a skin can refuse 60+ classes at once).
size_t pin_refusals_logged_=0;
bool RefusePin(const char* what,Object* object,const char* why) {
    if(pin_refusals_logged_>=12)return false;++pin_refusals_logged_;
    char state[640]{};ue::DescribeReference(object,state,sizeof(state));
    Logf("NPC_PIN %s refused (%s) %s",what,why,state);return false;
}
// bare_pins_: the classes of bare moves (a boss move granted without an NPC look, 不变身), held while one is granted or
// runs and released when it ends (ReleaseBarePins): never the look's skill_pins_, which only a look's Restore releases
// (with none on, they made HasResources true for good and every hero chain reset at its first bare move, 11-08).
std::vector<Identity> bare_pins_;
bool PinClass(Object* cls,std::vector<Identity>& pins=skill_pins_) {
    if(!cls)return false; // Not loaded: counted by the caller's loaded/pinned totals.
    if(!ClassRefsReady())return RefusePin("class",cls,"AddRef/ReleaseRef bytes");
    if(!ue::Alive(cls))return RefusePin("class",cls,"not alive");
    for(const Identity& pin:pins)if(pin.object==cls&&Same(pin))return true;
    Object* outer=ue::OuterOf(cls);
    if(pins.size()>=kMaxSkillPins)return RefusePin("class",cls,"ledger full");
    if(!ue::IsA(cls,Class("/Script/CoreUObject.Class")))return RefusePin("class",cls,"not a class");
    if(!ue::Alive(outer)||!ue::IsA(outer,Class("/Script/CoreUObject.Package"))||ue::OuterOf(outer))return RefusePin("class",cls,"outer is not a root package");
    const Identity id=Id(cls);if(!Same(id))return RefusePin("class",cls,"weak identity");
    reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base()+0x1573ff0)(cls);pins.push_back(id);return true;
}
void ReleaseBarePins() {
    if(bare_pins_.empty())return;
    if(!ClassRefsReady()){Logf("NPC_SKILL bare move class references kept: native helper bytes changed");return;}
    size_t released=0;for(const Identity& pin:bare_pins_)if(RawIdentity(pin,pin.index,pin.serial)){reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base()+0x158fa30)(pin.object);++released;}
    Logf("NPC_SKILL bare move class references released=%zu of %zu",released,bare_pins_.size());bare_pins_.clear();
}
void ReleasePins() {
    if(skill_pins_.empty())return;
    if(!ClassRefsReady()){Logf("NPC_SKILL class references kept: native helper bytes changed");return;}
    size_t released=0;for(const Identity& pin:skill_pins_)if(RawIdentity(pin,pin.index,pin.serial)){reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base()+0x158fa30)(pin.object);++released;}
    Logf("NPC_SKILL released class references=%zu of %zu",released,skill_pins_.size());skill_pins_.clear();
    trail_configs_.clear();trail_configs_for_=-1;trail_struct_=nullptr;
}
Object* PawnAsc(Object* pawn) {
    Call get(ue::Default("/Script/GameplayAbilities.Default__AbilitySystemBlueprintLibrary"),"GetAbilitySystemComponent");
    return get.Set("Actor",pawn).Invoke()?get.Get<Object*>("ReturnValue"):nullptr;
}
// The spec's ability: its live instance, or the class default while none exists; nullptr once the spec is gone.
Object* SpecAbility(Object* asc,int32_t handle,bool& instance) {
    instance=false;Call get(ue::Default("/Script/GameplayAbilities.Default__AbilitySystemBlueprintLibrary"),"GetGameplayAbilityFromSpecHandle");
    if(!get.Set("AbilitySystem",asc).Set("AbilitySpecHandle",handle).Invoke())return nullptr;
    instance=get.Get<bool>("bIsInstance");return get.Get<Object*>("ReturnValue");
}
// The abilities the ASC is running now (ActivatableAbilities rows with an ActiveCount), for a refusal log.
std::string ActiveSpecs(Object* asc) {
    Object* type=Class("/Script/GameplayAbilities.GameplayAbilitySpec"),*container=Class("/Script/GameplayAbilities.GameplayAbilitySpecContainer");
    const int32_t specs=Offset(asc,"ActivatableAbilities"),items=container?ue::Offset(container,"Items"):-1,size=type?ue::StructSize(type):0,
        ability_at=type?ue::Offset(type,"Ability"):-1,count_at=type?ue::Offset(type,"ActiveCount"):-1;
    if(specs<0||items<0||size<=0||ability_at<0||count_at<0)return "?";
    const Array array=ue::At<Array>(asc,specs+items);
    if(array.num<0||array.num>4096||array.max<array.num||(array.num&&!FaceReadable(array.data,static_cast<size_t>(array.num)*static_cast<size_t>(size))))return "?";
    std::string text;
    for(int32_t i=0;i<array.num;++i){Object* row=static_cast<Object*>(array.data)+static_cast<size_t>(i)*static_cast<size_t>(size);
        if(ue::At<uint8_t>(row,count_at))text+=(text.empty()?"":",")+Text(ue::At<Object*>(row,ability_at));}
    return text.empty()?"none":text;
}
// Why TryActivateAbility said no: the ability's required tags the hero lacks, blocked tags it has, and its cooldown / cost.
void LogActivationRefusal(Object* asc,Object* ability,const std::string& name) {
    static constexpr std::pair<const char*,bool> kChecks[]={{"ActivationRequiredTags",true},{"ActivationBlockedTags",false},{"SourceRequiredTags",true},{"SourceBlockedTags",false}};
    std::string text;
    for(const auto& [property,want]:kChecks){
        const Array tags=Property<Array>(ability,property);if(tags.num<=0||tags.num>64||!tags.data)continue;
        text+=std::string(text.empty()?"":" ")+property+"=[";
        for(int32_t i=0;i<tags.num;++i){const Name tag=static_cast<const Name*>(tags.data)[i];Call has(asc,"HasMatchingGameplayTag","/Script/GameplayTags.GameplayTagAssetInterface");
            const char* mark=has.Set("TagToCheck",tag).Invoke()?(has.Get<bool>("ReturnValue")==want?"":"!"):"?";text+=(i?",":"")+std::string(mark)+Text(tag);}
        text+="]";
    }
    Object* montage=nullptr;if(Object* anim=Same(primary_)?AnimInstance(primary_.object):nullptr){Call current(anim,"GetCurrentActiveMontage");if(current.Invoke())montage=current.Get<Object*>("ReturnValue");}
    Logf("NPC_SKILL refused ability=%s %s cooldown=%s cost=%s running=[%s] heroMontage=%s ('!' = the condition the hero fails)",name.c_str(),text.empty()?"tags=none":text.c_str(),
        Text(Property<Object*>(ability,"CooldownGameplayEffectClass")).c_str(),Text(Property<Object*>(ability,"CostGameplayEffectClass")).c_str(),ActiveSpecs(asc).c_str(),Text(montage).c_str());
}
// ---- The boss's weapons while one of its skills runs --------------------------------------------
bool IsWeaponActor(Object* actor) { return ue::Alive(actor)&&Offset(actor,"WeaponMesh")>=0; }
// Weapon actors attached anywhere under the hero (hands, back, scabbards), found as the grab add-on does.
std::vector<Object*> HeroWeaponActors(Object* pawn) {
    std::vector<Object*> result;Call attached(pawn,"GetAttachedActors");
    if(!attached.Set("bResetArray",true).Set("bRecursivelyIncludeAttachedActors",true).Invoke())return result;
    const Array list=attached.Get<Array>("OutActors");
    if(list.data&&list.num>0&&list.num<=256)for(int32_t i=0;i<list.num;++i){Object* actor=static_cast<Object**>(list.data)[i];if(IsWeaponActor(actor))result.push_back(actor);}
    ue::Free(list.data);return result;
}
bool IsEffect(Object* component) {
    Object* niagara=Class("/Script/Niagara.NiagaraComponent"),*cascade=Class("/Script/Engine.ParticleSystemComponent");
    return ue::Alive(component)&&((niagara&&ue::IsA(component,niagara))||(cascade&&ue::IsA(component,cascade)));
}
// An effect spawned on a weapon while the game runs (SpawnSystemAttached: a weapon buff's gameplay cue - GCC_WeaponEffect
// puts e.g. GC_HeiMoShang_WeaponBuff's blade fire on the avatar's Main weapon socket - or a trail), not one the weapon
// is built with: no default subobject (RF_DefaultSubObject 0x40000), no construction-script component
// (EComponentCreationMethod SimpleConstructionScript 1 / UserConstructionScript 2).
bool SpawnedEffect(Object* component) {
    if(!IsEffect(component)||(ue::At<uint32_t>(component,static_cast<int32_t>(ue::layout::kObjFlags))&0x40000u))return false;
    const int32_t at=Offset(component,"CreationMethod");
    return at<0||(ue::At<uint8_t>(component,at)!=1&&ue::At<uint8_t>(component,at)!=2);
}
// Every hero weapon actor is hidden for the skill one primitive component at a time, each one's own hidden
// state kept so the end shows only those that were visible. Not the actor: the boss skill's weapon effects
// spawn on the hero's weapon actors (MoveSkillEffects), and a component renders only while its owner actor is
// not hidden. A later pass (a weapon the skill attached since) hides new meshes only: an effect that appeared
// since is the skill's own. Shapes stay as they are (hidden in game anyway; the copied boss colliders are shapes).
// An effect spawned on a weapon at run time (SpawnedEffect: a weapon buff from an earlier move) is not part of the
// weapon: it is left shown and goes onto the boss weapon with the skill's effects (MoveWeaponEffects); before 11-12 it
// was hidden with the hero's weapon, so a buff vanished for every boss move.
// EnforceHiddenFlags hides a part again every frame if a skill shows one.
void HideHeroWeapons(Object* pawn=nullptr) {
    if(!pawn){if(!Same(pawn_))return;pawn=pawn_.object;}
    Object* primitive=Class("/Script/Engine.PrimitiveComponent"),*shape=Class("/Script/Engine.ShapeComponent");if(!primitive)return;
    for(Object* actor:HeroWeaponActors(pawn)){
        auto known=std::find_if(hidden_weapons_.begin(),hidden_weapons_.end(),[&](const HiddenWeapon& w){return w.actor.object==actor&&Same(w.actor);});
        const bool fresh=known==hidden_weapons_.end();
        if(fresh){hidden_weapons_.push_back({Id(actor),{},0});known=std::prev(hidden_weapons_.end());}
        for(Object* part:Components(actor,primitive)){
            if((shape&&ue::IsA(part,shape))||(IsEffect(part)&&(!fresh||SpawnedEffect(part))))continue;
            if(std::any_of(known->parts.begin(),known->parts.end(),[&](const HiddenPart& p){return p.component.object==part;}))continue;
            bool visible=false,hidden=false;if(!VisibilityOf(part,visible,hidden))continue;
            known->parts.push_back({Id(part),hidden});
            if(!hidden)Call(part,"SetHiddenInGame").Set("NewHidden",true).Set("bPropagateToChildren",false).Invoke();
        }
    }
}
void RestoreHeroWeapons() {
    size_t shown=0;
    for(const HiddenWeapon& w:hidden_weapons_)for(const HiddenPart& p:w.parts)
        if(!p.was_hidden&&Same(p.component)&&Call(p.component.object,"SetHiddenInGame").Set("NewHidden",false).Set("bPropagateToChildren",false).Invoke())++shown;
    if(!hidden_weapons_.empty())Logf("NPC_SKILL hero weapons shown again parts=%zu actors=%zu",shown,hidden_weapons_.size());
    hidden_weapons_.clear();
}
// FRotator (degrees) -> quaternion xyzw, exactly as UE's FRotator::Quaternion.
void RotatorQuat(double pitch,double yaw,double roll,double* q) {
    const double k=3.14159265358979323846/360.0,sp=std::sin(pitch*k),cp=std::cos(pitch*k),sy=std::sin(yaw*k),cy=std::cos(yaw*k),sr=std::sin(roll*k),cr=std::cos(roll*k);
    q[0]=cr*sp*sy-sr*cp*cy;q[1]=-cr*sp*cy-sr*cp*sy;q[2]=cr*cp*sy-sr*sp*cy;q[3]=cr*cp*cy+sr*sp*sy;
}
// Weapon configs name the bone (weapon_r) or a socket (weapon_r_Socket, weapon_rSocket); take what the display mesh has.
Name DisplaySocket(Object* mesh,const char* text) {
    const std::string base=text?text:"";std::vector<std::string> tries{base};
    const std::string lower=Lower(base);const size_t cut=lower.rfind("socket");
    if(cut!=std::string::npos&&cut+6==lower.size()){std::string stem=base.substr(0,cut);if(!stem.empty()&&stem.back()=='_')stem.pop_back();tries.push_back(stem);}
    else{tries.push_back(base+"_Socket");tries.push_back(base+"_socket");}
    for(const std::string& name:tries){const std::wstring wide(name.begin(),name.end());const Name candidate=ue::MakeName(wide.c_str());Call exists(mesh,"DoesSocketExist");
        if(!candidate.None()&&exists.Set("InSocketName",candidate).Invoke()&&exists.Get<bool>("ReturnValue"))return candidate;}
    return {};
}
// One boss weapon: its weapon actor's "Weapon Mesh" template (mesh, materials, relative placement) as a
// display-only component on the NPC display mesh, the boss's own skeleton, at the config's socket. on_hero: on a body
// that is not that boss's (a bare move: the hero's own mesh; another boss's move in a look: the look's display): the
// template's placement and size times `scale` (that body's size over the boss's), at the same socket or, when the body
// has none of that name, its weapon / hand socket of that side.
Name HeroWeaponSocket(Object* mesh,const char* text) {
    Name socket=DisplaySocket(mesh,text);
    if(!socket.None())return socket;
    const std::string low=Lower(text?text:"");
    const bool left=low.find("_l")!=std::string::npos||low.find("left")!=std::string::npos||(!low.empty()&&low.back()=='l');
    for(const char* name:left?std::initializer_list<const char*>{"weapon_l","hand_l"}:std::initializer_list<const char*>{"weapon_r","hand_r"})
        if(!(socket=DisplaySocket(mesh,name)).None())return socket;
    return {};
}
// A boss weapon's socket on the display: its own; for another boss's weapon (ForeignWeapons) as on the hero.
Name BossWeaponSocket(Object* display,const char* text){return ForeignWeapons()?HeroWeaponSocket(display,text):DisplaySocket(display,text);}
bool ShowSkillWeapon(const Context& context,Object* display,const NpcSkillWeapon& row,bool on_hero=false,double scale=1.0) {
    const std::string path=row.actor_class;const size_t dot=path.find('.');if(dot==std::string::npos)return false;
    if(!Class(path.c_str()))LoadClassPackage(path.substr(0,dot));
    Object* source=Property<Object*>(Class((path.substr(0,dot+1)+"Default__"+path.substr(dot+1)).c_str()),"WeaponMesh");
    Object* skeletal=Class("/Script/Engine.SkeletalMeshComponent"),*static_type=Class("/Script/Engine.StaticMeshComponent");
    const bool is_skeletal=ue::Alive(source)&&skeletal&&ue::IsA(source,skeletal),is_static=ue::Alive(source)&&!is_skeletal&&static_type&&ue::IsA(source,static_type);
    Object* asset=is_skeletal?SkelMesh(source):is_static?Property<Object*>(source,"StaticMesh"):nullptr;
    const Name socket=on_hero?HeroWeaponSocket(display,row.socket):DisplaySocket(display,row.socket);
    if(!ue::Alive(asset)||socket.None()){
        // A weapon blueprint whose WeaponMesh holds no mesh (fists, arm guards, body weapons) has nothing to show: its hit
        // shapes are placed through the template (AddSkillColliders); the audit does not count it as missing.
        const bool meshless=ue::Alive(source)&&!ue::Alive(asset);if(meshless)++meshless_rows_;
        Logf("NPC_SKILL boss weapon %s: template=%d mesh=%d socket %s on %s=%d; not shown%s",path.c_str(),ue::Alive(source)?1:0,ue::Alive(asset)?1:0,row.socket,Text(display).c_str(),socket.None()?0:1,
            meshless?" (mesh-less weapon: nothing to show)":"");
        return false;}
    struct Rotator { double pitch=0,yaw=0,roll=0; };
    const Vector at=Property<Vector>(source,"RelativeLocation"),size=Property<Vector>(source,"RelativeScale3D");const Rotator turn=Property<Rotator>(source,"RelativeRotation");
    Transform relative;RotatorQuat(turn.pitch,turn.yaw,turn.roll,relative.rotation);
    relative.translation[0]=at.x;relative.translation[1]=at.y;relative.translation[2]=at.z;relative.scale[0]=size.x;relative.scale[1]=size.y;relative.scale[2]=size.z;
    if(on_hero&&std::isfinite(scale)&&scale>0)for(size_t i=0;i<3;++i){relative.translation[i]*=scale;relative.scale[i]*=scale;}
    Object* component=Add(context.pawn,is_skeletal?skeletal:static_type,relative);if(!component)return false;
    skill_weapons_.push_back(Id(component)); // tracked first: RemoveSkillWeapons destroys it whatever fails below
    const bool mesh_set=is_skeletal?SetMesh(component,asset):Call(component,"SetStaticMesh").Set("NewMesh",asset).Invoke();
    // Rigid skeletal weapons (including the fire meteor heads) use the reference-pose render path on the boss.
    // A new SkeletalMeshComponent otherwise defaults to GPU skinning with no animation instance.
    bool render_static=false;
    if(is_skeletal&&Bool(source,"bRenderStatic",render_static)&&render_static){
        if(!Call(component,"SetRenderStatic").Set("bNewValue",true).Invoke())return false;
        Logf("NPC_SKILL weapon reference-pose rendering component=%s asset=%s",Text(component).c_str(),Text(asset).c_str());
    }
    Call(component,"SetCollisionEnabled").Set("NewType",uint8_t{0}).Invoke();
    const bool attached=AttachFinish(context.pawn,component,display,socket,relative);
    const bool dressed=attached&&CopyMaterials(source,component)&&MatchPlayerLighting(context.mesh,component);
    Logf("NPC_SKILL boss weapon %s %s socket=%s on %s mesh=%s meshSet=%d attached=%d materials+lighting=%d%s",attached&&mesh_set?"shown":"failed",path.c_str(),Text(socket).c_str(),
        Text(display).c_str(),Text(asset).c_str(),mesh_set?1:0,attached?1:0,dressed?1:0,on_hero?(" (不变身: fitted to this body, size x"+std::to_string(scale).substr(0,5)+")").c_str():"");
    return attached&&mesh_set;
}
// ---- Boss attack colliders ------------------------------------------------------------------------
// A sweep bullet (PBZ_SweepTraceBullet) takes its source components from its data's holder: the avatar's socket
// component, or each weapon actor WeaponUtils.GetWeaponActorsByDisplayIndex(avatar, slot, index) returns. On the
// actor owning each source it then sweeps every primitive component tagged AttachColliderData.ComponentTag
// (PBZ.exe 0x7d72110 -> AttachColliderHelper::GetAttachedCollidersAndShapes 0x5be6aa0 -> AActor::GetComponentsByTag).
// The hero's weapons carry only "Attack Collider" and the hero's pawn none, so the boss tags (Attack Collider
// Strong / Stab, LeftLeg, Big Arm, Snake Spine L ...) found nothing and those hits dealt no damage. For the skill,
// every Box / Sphere / Capsule template in the construction scripts of the boss weapon and of the boss tagged one
// of the skin's kNpcColliderTagSets is copied (shape, tags, collision profile): weapon ones owned by the hero
// weapon actors standing for that weapon, at the display's weapon socket (the boss weapon's root) or on the shown
// boss weapon mesh; body ones owned by the pawn, on the display's bone. RemoveSkillWeapons destroys them.
#pragma warning(push)
#pragma warning(disable: 4324) // Transform is 16-byte aligned.
struct ShapeTemplate { Object* component=nullptr;Transform relative;Name parent,socket;bool on_mesh=false; };
#pragma warning(pop)
bool HasSocket(Object* component,Name socket);
Transform TemplateRelative(Object* component) {
    struct Rotator { double pitch=0,yaw=0,roll=0; };
    const Vector at=Property<Vector>(component,"RelativeLocation"),size=Property<Vector>(component,"RelativeScale3D");const Rotator turn=Property<Rotator>(component,"RelativeRotation");
    Transform relative;RotatorQuat(turn.pitch,turn.yaw,turn.roll,relative.rotation);
    relative.translation[0]=at.x;relative.translation[1]=at.y;relative.translation[2]=at.z;relative.scale[0]=size.x;relative.scale[1]=size.y;relative.scale[2]=size.z;
    return relative;
}
bool TagWanted(Object* component,const NpcColliderTags& want,std::string& matched) {
    const Array tags=Property<Array>(component,"ComponentTags");
    if(tags.num<=0||tags.num>32||!tags.data)return false;
    for(int32_t i=0;i<tags.num;++i)for(size_t k=0;k<want.count;++k)if(ue::NameIs(static_cast<Name*>(tags.data)[i],want.tags[k])){matched=want.tags[k];return true;}
    return false;
}
// The tagged shape templates of a blueprint class's construction scripts (its own and its blueprint parents'),
// each placed relative to its nearest parent outside the scripts (parents inside them composed). on_mesh: that
// parent is mesh_name (the weapon's mesh / the character mesh) and socket is where it hangs on it.
std::vector<ShapeTemplate> ShapeTemplates(Object* cls,const NpcColliderTags& want,Name mesh_name,std::string& seen) {
    std::vector<ShapeTemplate> out;Object* shape=Class("/Script/Engine.ShapeComponent"),*generated=Class("/Script/Engine.BlueprintGeneratedClass");
    if(!shape||!generated)return out;
    struct Node { Object* component;Name variable,parent,socket; };std::vector<Node> nodes;
    for(Object* c=cls;c&&ue::IsA(c,generated)&&nodes.size()<256;c=ue::At<Object*>(c,ue::layout::kSuper)){
        Object* script=Property<Object*>(c,"SimpleConstructionScript");if(!ue::Alive(script))continue;
        const Array all=Property<Array>(script,"AllNodes");if(all.num<=0||all.num>256||!all.data)continue;
        for(int32_t i=0;i<all.num;++i){Object* n=static_cast<Object**>(all.data)[i];if(!ue::Alive(n))continue;
            nodes.push_back({Property<Object*>(n,"ComponentTemplate"),Property<Name>(n,"InternalVariableName"),Property<Name>(n,"ParentComponentOrVariableName"),Property<Name>(n,"AttachToName")});}
    }
    for(const Node& n:nodes){
        std::string tag;if(!ue::Alive(n.component)||!ue::IsA(n.component,shape)||!TagWanted(n.component,want,tag))continue;
        ShapeTemplate s;s.component=n.component;s.relative=TemplateRelative(n.component);s.socket=n.socket;Name parent=n.parent;
        for(size_t depth=0;depth<8;++depth){
            if(!mesh_name.None()&&parent==mesh_name){s.on_mesh=true;break;}
            const auto up=std::find_if(nodes.begin(),nodes.end(),[&](const Node& m){return !parent.None()&&m.variable==parent;});
            if(up==nodes.end()||!ue::Alive(up->component))break;
            Transform composed;if(!Compose(s.relative,TemplateRelative(up->component),composed))break;
            s.relative=composed;s.socket=up->socket;parent=up->parent;
        }
        s.parent=parent;out.push_back(s);if(seen.find(tag)==std::string::npos)seen+=(seen.empty()?"":",")+tag;
    }
    return out;
}
// Tags written into an array the game allocated (a skeletal mesh's GetAllSocketNames, cut down) so the component frees
// it as its own. The mesh: the look's display, else the body the move plays on (WeaponDisplay: a 不变身 move's is the
// hero's own mesh). Before 11-12 only a look's display was taken, so a 不变身 move tagged none of the boss colliders it
// copied: each was made but left untagged and unattached (NPC_SKILL boss colliders weapon=0 body=0 tracked=5), and
// the move swept with the hidden hero sword's own shapes instead of the boss weapon's.
bool WriteTags(Object* to,const Name* tags,int32_t count) {
    const int32_t at=Offset(to,"ComponentTags");
    Object* host=Same(display_)?display_.object:WeaponDisplay();
    if(at<0||count<=0||count>32||!tags||!ue::Alive(host))return false;
    Call names(host,"GetAllSocketNames");if(!names.Invoke())return false;
    Array storage=names.Get<Array>("ReturnValue");
    if(storage.num<count||!storage.data){ue::Free(storage.data);return false;}
    std::memcpy(storage.data,tags,sizeof(Name)*static_cast<size_t>(count));storage.num=count;
    Array& dest=ue::At<Array>(to,at);if(dest.data)ue::Free(dest.data);dest=storage;return true;
}
bool CopyTags(Object* from,Object* to) {
    const Array tags=Property<Array>(from,"ComponentTags");
    return tags.num>0&&tags.num<=32&&tags.data&&WriteTags(to,static_cast<const Name*>(tags.data),tags.num);
}
// One shape template copied onto owner (shape, tags, collision profile; never physics), on parent at socket.
// only_tag: the copy carries that one tag instead of the template's (a source missing a sweep's tag, CoverSweepTags).
Object* CopyShape(Object* owner,const ShapeTemplate& s,Object* parent,Name socket,Name only_tag=Name{}) {
    Object* source=s.component,*box=Class("/Script/Engine.BoxComponent"),*sphere=Class("/Script/Engine.SphereComponent"),*capsule=Class("/Script/Engine.CapsuleComponent");
    Object* component=Add(owner,ue::ClassOf(source),s.relative);if(!component)return nullptr;
    skill_colliders_.push_back(Id(component)); // tracked first: RemoveSkillWeapons destroys it whatever fails below
    if(box&&ue::IsA(source,box))Call(component,"SetBoxExtent").Set("InBoxExtent",Property<Vector>(source,"BoxExtent")).Set("bUpdateOverlaps",false).Invoke();
    else if(sphere&&ue::IsA(source,sphere))Call(component,"SetSphereRadius").Set("InSphereRadius",Property<float>(source,"SphereRadius")).Set("bUpdateOverlaps",false).Invoke();
    else if(capsule&&ue::IsA(source,capsule))Call(component,"SetCapsuleSize").Set("InRadius",Property<float>(source,"CapsuleRadius")).Set("InHalfHeight",Property<float>(source,"CapsuleHalfHeight")).Set("bUpdateOverlaps",false).Invoke();
    Call profile(source,"GetCollisionProfileName");
    if(profile.Invoke())Call(component,"SetCollisionProfileName").Set("InCollisionProfileName",profile.Get<Name>("ReturnValue")).Set("bUpdateOverlaps",false).Invoke();
    Call enabled(source,"GetCollisionEnabled");const uint8_t mode=enabled.Invoke()?enabled.Get<uint8_t>("ReturnValue"):0;
    Call(component,"SetCollisionEnabled").Set("NewType",static_cast<uint8_t>(mode==3?1:mode==2?0:mode)).Invoke();
    const bool tagged=only_tag.None()?CopyTags(source,component):WriteTags(component,&only_tag,1);
    return tagged&&AttachFinish(owner,component,parent,socket,s.relative)?component:nullptr;
}
// WeaponUtils.GetWeaponActorsByDisplayIndex(Avatar, SlotTag, DisplayIndex, WithPending, OutActors): the hero weapon
// actors a weapon-held bullet or effect of that display index uses (WithPending false, as the sweep bullet passes it).
// Before c75 this wanted exactly four parameters, so it always came back empty: the colliders went onto every hidden
// hero weapon and weaponGaps listed index 0 too.
std::vector<Object*> SlotWeaponActors(Object* pawn,int32_t display) {
    std::vector<Object*> result;Object* utils=ue::Default("/Script/SGameCombatSystem.Default__WeaponUtils");
    Object* function=utils?ue::FindFunction(ue::ClassOf(utils),"GetWeaponActorsByDisplayIndex"):nullptr;if(!function)return result;
    std::map<std::string,int32_t> params;
    for(uintptr_t f=ue::At<uintptr_t>(function,ue::layout::kChildProperties);f&&params.size()<8;f=ue::At<uintptr_t>(reinterpret_cast<void*>(f),ue::layout::kPropNext))
        params[Text(ue::At<Name>(reinterpret_cast<void*>(f),ue::layout::kPropName))]=ue::At<int32_t>(reinterpret_cast<void*>(f),ue::layout::kPropElementSize);
    const auto has=[&](const char* name,int32_t size){const auto p=params.find(name);return p!=params.end()&&p->second==size;};
    if(!has("Avatar",8)||!has("SlotTag",8)||!has("DisplayIndex",4)||!has("OutActors",16)||(params.size()!=4&&!(params.size()==5&&has("WithPending",1))))return result;
    const Name main=ue::MakeName(L"Weapon.Slot.Main");if(main.None())return result;
    Call get(utils,function);get.Set("Avatar",pawn).Set("SlotTag",main).Set("DisplayIndex",display);
    if(params.size()==5)get.Set("WithPending",false);
    if(!get.Invoke())return result;
    const Array list=get.Get<Array>("OutActors");
    if(list.num>0&&list.num<=16&&list.data)for(int32_t i=0;i<list.num;++i){Object* actor=static_cast<Object**>(list.data)[i];if(ue::Alive(actor))result.push_back(actor);}
    ue::Free(list.data);return result;
}
// ---- Weapon display-index stand-ins -----------------------------------------------------------------
// GetWeaponActorsByDisplayIndex(avatar, slot, k) is WeaponActors[k] of the avatar's current weapon for the slot (all
// of them for k < 0): CharacterWeaponsComponent.RuntimeWeaponData[slot].Weapons[current].WeaponActors (PBZ.exe
// 0x5e96020 -> 0x5e76f20 -> RuntimeWeaponSingleData +0x10). Bosses with two or more weapons address the others as
// display index 1..3 (沐小葵 double swords 2, 钩爪客 1..3, 乱世 / 铁鬼 1..3, 鬼差 1 ...: 302 skills of 25 skins); the
// hero's weapon has index 0 only, so those weapon-held sweeps found no source and dealt no damage. While a skill
// runs, every index its skin's skills address (kNpcWeaponDisplays, at least 0) that the entry the lookup reads
// lacks gets a plain actor (scene root at the boss weapon's socket on the display) appended to that WeaponActors array
// (reflection + GenericArray_Add, no native hook); AddSkillColliders copies the boss weapon's attack colliders
// onto it, and the skill's end removes and destroys it. It is no weapon actor (no SocketComponentHolder): weapon
// effects of index >= 1 still have no socket to spawn on, and an all-weapons (-1) effect still spawns once.
// The hero's Main weapon slot (RuntimeWeaponData: TMap<GameplayTag, RuntimeWeaponSlotData{Weapons:
// TArray<RuntimeWeaponSingleData>}>): its weapon entries, and per entry the WeaponActors array with that array's
// FArrayProperty for GenericArray_Add / Remove. Layout from reflection; no entries when anything does not match.
struct MainSlot {
    Object* weapons=nullptr;const Array* entries=nullptr;int32_t entry_size=0,actors_at=0;uintptr_t field=0;
    int32_t Count() const { return entries?entries->num:0; }
    Array* Actors(int32_t e) const {
        if(!entries||e<0||e>=entries->num)return nullptr;
        Array* members=reinterpret_cast<Array*>(static_cast<unsigned char*>(entries->data)+size_t(e)*size_t(entry_size)+actors_at);
        return members->num>=0&&members->num<=members->max&&members->max<=64&&(!members->num||FaceReadable(members->data,size_t(members->num)*sizeof(Object*)))?members:nullptr;
    }
    int32_t Holding(Object* member) const {
        for(int32_t e=0;e<Count();++e)if(const Array* a=Actors(e))for(int32_t m=0;m<a->num;++m)if(static_cast<Object**>(a->data)[m]==member)return e;
        return -1;
    }
};
MainSlot FindMainSlot(Object* pawn) {
    MainSlot out;static const Name main=ue::MakeName(L"Weapon.Slot.Main");if(main.None()||!ue::Alive(pawn))return out;
    Call get(pawn,"GetCharacterWeaponsComponent","/Script/SGameCombatSystem.CharacterWeaponsHolder");
    Object* weapons=get.Invoke()?get.Get<Object*>("ReturnValue"):nullptr;if(!ue::Alive(weapons))return out;out.weapons=weapons;
    const auto prop=[](uintptr_t field){return reinterpret_cast<Object*>(field);};
    const uintptr_t map=Field(ue::ClassOf(weapons),"RuntimeWeaponData");if(!map||FieldKind(prop(map))!=0x400000000000ull)return out;
    Object* key=ue::At<Object*>(prop(map),0x70),*value=ue::At<Object*>(prop(map),0x78);
    if(!key||!value||FieldKind(key)!=0x100000||FieldKind(value)!=0x100000||Text(ue::At<Object*>(key,0x70))!="GameplayTag")return out;
    const uintptr_t list=Field(ue::At<Object*>(value,0x70),"Weapons");if(!list||FieldKind(prop(list))!=0x200000)return out;
    Object* inner=ue::At<Object*>(prop(list),0x78);if(!inner||FieldKind(inner)!=0x100000)return out;
    const uintptr_t actors=Field(ue::At<Object*>(inner,0x70),"WeaponActors");if(!actors||FieldKind(prop(actors))!=0x200000)return out;
    Object* element=ue::At<Object*>(prop(actors),0x78);if(!element||ue::At<int32_t>(element,ue::layout::kPropElementSize)!=8)return out;
    const int32_t entry_size=ue::At<int32_t>(inner,ue::layout::kPropElementSize),value_offset=ue::At<int32_t>(prop(map),0x80),stride=ue::At<int32_t>(prop(map),0x8c);
    const int32_t list_at=ue::At<int32_t>(prop(list),ue::layout::kPropOffset),actors_at=ue::At<int32_t>(prop(actors),ue::layout::kPropOffset);
    const unsigned char* at=reinterpret_cast<const unsigned char*>(weapons)+ue::At<int32_t>(prop(map),ue::layout::kPropOffset);
    const int32_t count=*reinterpret_cast<const int32_t*>(at+8),bits=*reinterpret_cast<const int32_t*>(at+40);
    unsigned char* data=*reinterpret_cast<unsigned char* const*>(at);const uint32_t* allocation=*reinterpret_cast<uint32_t* const*>(at+32);
    if(!allocation)allocation=reinterpret_cast<const uint32_t*>(at+16);
    if(entry_size<=0||entry_size>4096||stride<16||stride>4096||value_offset<8||value_offset>=stride||count<=0||count>64||bits<count||
        !FaceReadable(data,size_t(count)*size_t(stride))||!FaceReadable(allocation,(size_t(count)+31)/32*4))return out;
    for(int32_t i=0;i<count;++i){
        unsigned char* row=data+size_t(i)*size_t(stride);
        if(!(allocation[i/32]&(1u<<(i%32)))||!(*reinterpret_cast<const Name*>(row)==main))continue;
        const Array* entries=reinterpret_cast<const Array*>(row+value_offset+list_at);
        if(entries->num<=0||entries->num>64||!FaceReadable(entries->data,size_t(entries->num)*size_t(entry_size)))return out;
        out.entries=entries;out.entry_size=entry_size;out.actors_at=actors_at;out.field=actors;return out;
    }
    return out;
}
bool CurrentMainWeapon(Object* weapons,int32_t& index);
// A plain actor (scene root) owned by the hero, its root snapped to parent's socket.
Object* SpawnStandIn(Object* pawn,Object* parent,Name socket) {
    Object* statics=ue::Default("/Script/Engine.Default__GameplayStatics"),*actor_class=Class("/Script/Engine.Actor"),*scene=Class("/Script/Engine.SceneComponent");
    if(!statics||!actor_class||!scene||!ue::Alive(parent))return nullptr;
    Transform at;{Call get(pawn,"GetTransform");if(get.Invoke())at=get.Get<Transform>("ReturnValue");}
    Call begin(statics,"BeginDeferredActorSpawnFromClass");
    if(!begin.Set("WorldContextObject",pawn).Set("ActorClass",actor_class).Set("SpawnTransform",at).Set("CollisionHandlingOverride",uint8_t{1}).Set("Owner",pawn).Invoke())return nullptr;
    Object* actor=begin.Get<Object*>("ReturnValue");if(!ue::Alive(actor))return nullptr;
    const auto drop=[&](const char* why){Logf("NPC_SKILL stand-in %s: %s; destroyed",Text(actor).c_str(),why);Call(actor,"K2_DestroyActor").Invoke();return nullptr;};
    if(!Call(statics,"FinishSpawningActor").Set("Actor",actor).Set("SpawnTransform",at).Invoke())return drop("FinishSpawningActor refused");
    Call add(actor,"AddComponentByClass");
    if(!add.Set("Class",scene).Set("bManualAttachment",false).Set("RelativeTransform",Transform{}).Set("bDeferredFinish",false).Invoke())return drop("AddComponentByClass refused");
    Object* root=Property<Object*>(actor,"RootComponent");if(!ue::Alive(root)||root!=add.Get<Object*>("ReturnValue"))return drop("no scene root");
    Call attach(root,"K2_AttachToComponent"); // EAttachmentRule::SnapToTarget = 2
    if(!attach.Set("Parent",parent).Set("SocketName",socket).Set("LocationRule",uint8_t{2}).Set("RotationRule",uint8_t{2}).Set("ScaleRule",uint8_t{2}).Set("bWeldSimulatedBodies",false).Invoke()||
        !attach.Get<bool>("ReturnValue"))return drop("attach refused");
    return actor;
}
// The entry the native lookup reads: the one holding what GetWeaponActorsByDisplayIndex(-1) returns, else the
// slot's current one (GetCurrentWeapon). Every index below max(kNpcWeaponDisplays, 1) it lacks gets a stand-in,
// index 0 too when that entry has no actor at all.
void AddStandIns(const Context& context) {
    const int32_t skin=WeaponSkin();
    if(!stand_ins_.empty()||skin<0||static_cast<size_t>(skin)>=std::size(kNpcWeaponDisplays)||!WeaponDisplay())return;
    const int32_t want=(std::max)(int32_t{kNpcWeaponDisplays[skin]},1);
    const MainSlot slot=FindMainSlot(context.pawn);const std::vector<Object*> all=SlotWeaponActors(context.pawn,-1);
    stand_in_host_={};for(Object* actor:SlotWeaponActors(context.pawn,0))if(IsWeaponActor(actor)){stand_in_host_=Id(actor);break;}
    int32_t current=-1;const bool known=slot.weapons&&CurrentMainWeapon(slot.weapons,current);
    const int32_t used=!all.empty()?slot.Holding(all.front()):known&&current<slot.Count()?current:-1;
    std::string layout;
    for(int32_t e=0;e<slot.Count()&&layout.size()<400;++e){const Array* a=slot.Actors(e);char row[160]{};
        _snprintf_s(row,sizeof(row),_TRUNCATE,"%s%d:%d:%s",layout.empty()?"":" ",e,a?a->num:-1,a&&a->num?Text(static_cast<Object**>(a->data)[0]).c_str():"-");layout+=row;}
    Array* target=slot.Actors(used);
    Logf("NPC_SKILL hero Main slot entries=%d current=%d%s lookupAll=%zu used=%d [%s]",slot.Count(),current,known?"":"(unknown)",all.size(),used,layout.empty()?"-":layout.c_str());
    if(!target||target->num>=want||!NativeHelpersMatch()){if(!target)Logf("NPC_SKILL stand-ins: none (no entry the lookup reads; array helpers=%d)",NativeHelpersMatch()?1:0);return;}
    const WeaponRows set=Rows();const int32_t had=target->num;
    for(int32_t k=had;k<want;++k){
        // At the boss weapon of that index, else at the first one's socket (an index of a weapon in another config).
        const size_t row=static_cast<size_t>(k)<set.count?static_cast<size_t>(k):0;
        const Name socket=set.count?BossWeaponSocket(WeaponDisplay(),set.weapons[row].socket):Name{};
        Object* actor=SpawnStandIn(context.pawn,WeaponDisplay(),socket);if(!actor)break;
        stand_ins_.push_back({Id(actor),k}); // tracked first: RemoveStandIns destroys it whatever fails below
        const int32_t at=reinterpret_cast<int32_t(__fastcall*)(void*,const void*,const void*)>(ue::Base()+kArrayAddRva)(target,reinterpret_cast<void*>(slot.field),&actor);
        if(at!=k||target->num!=k+1||static_cast<Object**>(target->data)[k]!=actor){Logf("NPC_SKILL stand-in %d: array add returned %d (num %d); stopped",k,at,target->num);break;}
    }
    int32_t resolved=0;for(int32_t k=0;k<want;++k)if(!SlotWeaponActors(context.pawn,k).empty())++resolved; // the native lookup now finds them
    Logf("NPC_SKILL stand-ins for display %d..%d: %zu added in entry %d on %s; lookup resolves %d/%d",had,want-1,stand_ins_.size(),used,Text(WeaponDisplay()).c_str(),resolved,want);
}
// Out of the hero's WeaponActors (last added first, so the indices before stay) and destroyed.
void RemoveStandIns() {
    if(stand_ins_.empty())return;
    size_t removed=0,destroyed=0;
    for(auto s=stand_ins_.rbegin();s!=stand_ins_.rend();++s){
        if(!Same(s->actor))continue;
        const MainSlot slot=WeaponPawn()!=nullptr&&NativeHelpersMatch()?FindMainSlot(WeaponPawn()):MainSlot{};
        if(Array* list=slot.Actors(slot.Holding(s->actor.object)))for(int32_t i=list->num-1;i>=0;--i)if(static_cast<Object**>(list->data)[i]==s->actor.object){
            reinterpret_cast<void(__fastcall*)(void*,const void*,int32_t)>(ue::Base()+kArrayRemoveRva)(list,reinterpret_cast<void*>(slot.field),i);++removed;break;}
        if(Call(s->actor.object,"K2_DestroyActor").Invoke())++destroyed;
    }
    Logf("NPC_SKILL stand-ins removed=%zu destroyed=%zu of %zu",removed,destroyed,stand_ins_.size());stand_ins_.clear();stand_in_host_={};
}
// Boss weapon row k's class and its "Weapon Mesh" component template (nullptr when not loaded).
Object* WeaponClass(size_t k,Object** mesh=nullptr) {
    const WeaponRows rows=Rows();if(k>=rows.count)return nullptr;
    const std::string path=rows.weapons[k].actor_class;const size_t dot=path.find('.');Object* cls=dot==std::string::npos?nullptr:Class(path.c_str());
    if(cls&&mesh)*mesh=Property<Object*>(Class((path.substr(0,dot+1)+"Default__"+path.substr(dot+1)).c_str()),"WeaponMesh");
    return cls;
}
std::vector<ShapeTemplate> WeaponShapeTemplates(size_t k,const NpcColliderTags& want,std::string& seen) {
    Object* mesh=nullptr;Object* cls=WeaponClass(k,&mesh);
    return cls?ShapeTemplates(cls,want,ue::Alive(mesh)?ue::NameOf(mesh):Name{},seen):std::vector<ShapeTemplate>{};
}
// Shapes of boss weapon row `from` onto owner where boss weapon row k hangs: on its shown mesh, else at the display's
// weapon socket. A boss weapon without a mesh (fists, 拳套 configs) shows none: a shape on its mesh root is then placed
// through the mesh template's own placement under that socket (one on a mesh socket has nowhere to go).
size_t CopyWeaponShapes(size_t k,size_t from,const std::vector<ShapeTemplate>& shapes,Object* owner,Name only_tag=Name{}) {
    const WeaponRows rows=Rows();if(k>=rows.count||!WeaponDisplay())return 0;
    const Name root=BossWeaponSocket(WeaponDisplay(),rows.weapons[k].socket);
    Object* shown=k<boss_weapon_rows_.size()&&Same(boss_weapon_rows_[k])?boss_weapon_rows_[k].object:nullptr;
    Object* mesh=nullptr;WeaponClass(from,&mesh);size_t copied=0;
    for(const ShapeTemplate& s:shapes){
        ShapeTemplate placed=s;Object* parent=WeaponDisplay();Name socket=root;
        if(s.on_mesh&&shown){parent=shown;socket=s.socket;}
        else if(s.on_mesh&&(!s.socket.None()||!ue::Alive(mesh)||!Compose(s.relative,TemplateRelative(mesh),placed.relative)))continue;
        if((parent==shown||!root.None())&&CopyShape(owner,placed,parent,socket,only_tag))++copied;
    }
    return copied;
}
// The boss body's shapes tagged want onto the pawn: on the display's bone, else on the pawn's root.
size_t CopyBodyShapes(Object* pawn,const NpcColliderTags& want,std::string& tags,Name only_tag=Name{}) {
    const int32_t skin=WeaponSkin();
    if(skin<0||static_cast<size_t>(skin)>=std::size(kNpcSkillSets)||!WeaponDisplay())return 0;
    const NpcSkillSet& set=kNpcSkillSets[skin];
    Object* monster=set.monster&&*set.monster?LoadClassPackage(set.monster):nullptr,*root=Property<Object*>(pawn,"RootComponent");size_t copied=0;
    if(monster)for(const ShapeTemplate& s:ShapeTemplates(monster,want,ue::MakeName(L"CharacterMesh0"),tags)){
        Object* parent=s.on_mesh?WeaponDisplay():root;const Name socket=s.on_mesh?s.socket:Name{};
        if(s.on_mesh&&!socket.None()&&!HasSocket(WeaponDisplay(),socket))continue;
        if(ue::Alive(parent)&&CopyShape(pawn,s,parent,socket,only_tag))++copied;
    }
    return copied;
}
void AddSkillColliders(const Context& context) {
    const int32_t skin=WeaponSkin();
    if(skin<0||static_cast<size_t>(skin)>=std::size(kNpcColliderTagSets)||!WeaponDisplay())return;
    const NpcColliderTags& want=kNpcColliderTagSets[skin];const WeaponRows set=Rows();
    if(!want.count)return;
    size_t weapon=0,body=0,owners=0;std::string tags;
    std::vector<std::vector<ShapeTemplate>> shapes(set.count);
    const auto copy_weapon=[&](size_t k,Object* owner){weapon+=CopyWeaponShapes(k,k,shapes[k],owner);};
    for(size_t k=0;k<set.count;++k){
        shapes[k]=WeaponShapeTemplates(k,want,tags);if(shapes[k].empty())continue;
        // The hero weapons (or stand-in) of the same display index stand for this boss weapon (its effects go to it
        // too); with none there, the first ones (or every hero weapon) also take its colliders.
        std::vector<Object*> actors=SlotWeaponActors(context.pawn,static_cast<int32_t>(k));const bool exact=!actors.empty();
        if(actors.empty()&&k)actors=SlotWeaponActors(context.pawn,0);
        if(actors.empty())for(const HiddenWeapon& w:hidden_weapons_)if(Same(w.actor))actors.push_back(w.actor.object);
        for(Object* actor:actors){
            if(exact)for(HiddenWeapon& w:hidden_weapons_)if(w.actor.object==actor)w.boss=k;
            ++owners;copy_weapon(k,actor);
        }
    }
    // A stand-in past the boss's listed weapons is a weapon of another config (沐小葵's second sword, 赤练's second
    // snake ...): it takes every boss weapon's colliders, or with no boss weapon (an unarmed boss) the hero weapon's own.
    for(const StandIn& s:stand_ins_){
        if(!Same(s.actor)||static_cast<size_t>(s.index)<set.count)continue;
        ++owners;const size_t before=weapon;
        for(size_t k=0;k<set.count;++k)copy_weapon(k,s.actor.object);
        // The hero's own Main weapon: the one shown before the skill (its parts were visible when hidden).
        static const Name main=ue::MakeName(L"Weapon.Slot.Main");Object* shape=Class("/Script/Engine.ShapeComponent");
        if(weapon==before&&shape)for(const HiddenWeapon& w:hidden_weapons_){
            if(!Same(w.actor)||!(Property<Name>(w.actor.object,"Slot")==main)||std::none_of(w.parts.begin(),w.parts.end(),[](const HiddenPart& p){return !p.was_hidden;}))continue;
            for(Object* c:Components(w.actor.object,shape)){
                std::string tag;if(!TagWanted(c,want,tag)||std::any_of(skill_colliders_.begin(),skill_colliders_.end(),[&](const Identity& x){return x.object==c;}))continue;
                ShapeTemplate copy;copy.component=c;if(CopyShape(s.actor.object,copy,c,Name{}))++weapon;
                if(tags.find(tag)==std::string::npos)tags+=(tags.empty()?"":",")+tag;
            }
        }
    }
    body=CopyBodyShapes(context.pawn,want,tags);
    Logf("NPC_SKILL boss colliders weapon=%zu (hero weapon actors %zu) body=%zu tags=%s tracked=%zu",weapon,owners,body,tags.empty()?"-":tags.c_str(),skill_colliders_.size());
}
// ---- Boss skill effects ---------------------------------------------------------------------------
// The weapon effect actions (GAAction_WeaponTrail / WeaponParticle / WeaponParticleFrame, GAA_PBZWeaponTrail ...)
// spawn on WeaponUtils.GetWeaponActorsByDisplayIndex(avatar, slot, index)'s SocketComponentHolder component at the
// action's socket: on a skin that is the hero's weapon mesh, so they stayed with the hero's hidden weapon (and,
// while the whole actor was hidden, never rendered). Avatar-held effects and the montage's notify effects attach
// to the hero's own mesh at the boss skeleton's sockets. Every frame of a skill a Niagara / Cascade component that
// appears under a hero weapon moves onto the boss weapon it stands for (same socket, same relative transform), and
// one under the hero's mesh moves onto the display at its socket unless only the hero's skeleton has that socket.
// A boss skill's GAA_SwitchWeapon (44 skills: 沐小葵 P2's double swords, ...) sets the avatar's current weapon to
// the boss's weapon config index (CharacterWeaponsComponent.SetCurrentWeapon + SetArmed) and back at its end.
// On the hero it switched the hero's own weapon mid-skill, and the sweeps then looked for their colliders on
// another (sheathed) weapon. While a skill runs the hero's current Main weapon stays the one it began with.
struct HeroWeaponHold { Identity weapons;int32_t index=-1;uint32_t restored=0; };
HeroWeaponHold weapon_hold_;
bool CurrentMainWeapon(Object* weapons,int32_t& index) {
    static const Name main=ue::MakeName(L"Weapon.Slot.Main");if(main.None()||!ue::Alive(weapons))return false;
    Call get(weapons,"GetCurrentWeapon");if(!get.Set("SlotTag",main).Invoke())return false;
    index=get.Get<int32_t>("CurrentIndex");return index>=0&&index<64;
}
void HoldHeroWeapon(bool begin) {
    if(begin){weapon_hold_={};Call get(WeaponPawn(),"GetCharacterWeaponsComponent","/Script/SGameCombatSystem.CharacterWeaponsHolder");
        Object* weapons=WeaponPawn()!=nullptr&&get.Invoke()?get.Get<Object*>("ReturnValue"):nullptr;int32_t index=-1;
        if(CurrentMainWeapon(weapons,index)){weapon_hold_.weapons=Id(weapons);weapon_hold_.index=index;}return;}
    int32_t now=-1;if(weapon_hold_.index<0||!Same(weapon_hold_.weapons)||!CurrentMainWeapon(weapon_hold_.weapons.object,now)||now==weapon_hold_.index)return;
    static const Name main=ue::MakeName(L"Weapon.Slot.Main");Call set(weapon_hold_.weapons.object,"SetCurrentWeapon");
    const bool ok=set.Set("SlotTag",main).Set("WeaponIndex",weapon_hold_.index).Invoke()&&set.Get<bool>("ReturnValue");
    if(++weapon_hold_.restored<=3)Logf("NPC_SKILL hero weapon switched by %s: Main %d -> %d, put back=%d",skill_.name.empty()?"the skill tail":skill_.name.c_str(),weapon_hold_.index,now,ok?1:0);
}
std::vector<Object*> Children(Object* component) {
    std::vector<Object*> out;Call get(component,"GetChildrenComponents");if(!get.Set("bIncludeAllDescendants",false).Invoke())return out;
    const Array list=get.Get<Array>("Children");
    if(list.num>0&&list.num<=512&&list.data)for(int32_t i=0;i<list.num;++i){Object* c=static_cast<Object**>(list.data)[i];if(ue::Alive(c))out.push_back(c);}
    ue::Free(list.data);return out;
}
// Handled already and still where it was put or left: a pooled effect the game re-attached under a hero part is new.
bool Seen(Object* effect,Object* holder) {
    return std::any_of(seen_effects_.begin(),seen_effects_.end(),[&](const SeenEffect& s){return s.effect.object==effect&&s.parent==holder&&Same(s.effect);});
}
bool HasSocket(Object* component,Name socket) {
    if(socket.None()||!ue::Alive(component))return false;
    Call exists(component,"DoesSocketExist");return exists.Set("InSocketName",socket).Invoke()&&exists.Get<bool>("ReturnValue");
}
// A boss socket the NPC body lacks can sit on a boss weapon or a part attached to the body (MoYuan's
// TrailStart2 trail starts): that component, else none. Only a component that really has the socket is
// returned; a weapon whose child carries it yields the child, so the socket name resolves where it is used.
Object* BossSocketHolder(Name socket) {
    for(const Identity& w:skill_weapons_)if(Same(w)){
        if(HasSocket(w.object,socket))return w.object;
        for(Object* c:Children(w.object))if(ue::Alive(c)&&!IsEffect(c)&&HasSocket(c,socket))return c;
    }
    if(WeaponDisplay()!=nullptr)for(Object* c:Children(WeaponDisplay()))if(!IsEffect(c)&&HasSocket(c,socket))return c;
    return nullptr;
}
// Where an effect that hangs on a socket its intended parent lacks should go. A weapon effect (spawned on a
// hero weapon) looks for the socket on the boss weapons first, a body effect (spawned on the hero mesh) on the
// NPC body first; the other side is the fallback. nullptr means nothing has the socket.
Object* SocketHost(Name socket,bool prefer_weapon) {
    if(socket.None())return nullptr;
    const auto body=[&]()->Object*{return WeaponDisplay()!=nullptr&&HasSocket(WeaponDisplay(),socket)?WeaponDisplay():nullptr;};
    if(prefer_weapon){if(Object* w=BossSocketHolder(socket))return w;return body();}
    if(Object* b=body())return b;return BossSocketHolder(socket);
}
void MoveEffect(Object* effect,Object* holder,Object* parent,bool keep_hero_socket) {
    if(seen_effects_.size()>=512)seen_effects_.erase(seen_effects_.begin(),seen_effects_.begin()+256);
    Call get(effect,"GetAttachSocketName");const Name socket=get.Invoke()?get.Get<Name>("ReturnValue"):Name{};
    // A named socket the intended parent lacks: the boss's sockets live on its skeleton and weapons, not on
    // the hero's, so look for the component that really owns it first (weapon effect: boss weapons; body
    // effect: the NPC body). Attach rules are EAttachmentRule {KeepRelative, KeepWorld, SnapToTarget}.
    bool has=HasSocket(parent,socket);const uint8_t rule=0; // relative on purpose: a socket-less effect keeps its offset
    if(!socket.None()&&!has){
        // The boss's sockets live on its skeleton and weapons, not on the hero's: look for the component that
        // really owns the name (a weapon effect: the boss weapons first; a body effect: the NPC body).
        if(Object* other=SocketHost(socket,!keep_hero_socket)){
            parent=other;has=HasSocket(parent,socket);
        }else if(++unplaced_effects_<=12)Logf("NPC_SKILL effect %s: no %s socket on the NPC body or a boss weapon; at the parent root",
            Text(effect).c_str(),Text(socket).c_str());
    }
    // Recorded against the component it was found under, not where it was put: the next frame's Seen() check
    // walks the same containers, and an effect moved elsewhere must not be treated as a new one and moved again.
    seen_effects_.push_back({Id(effect),holder});
    Call attach(effect,"K2_AttachToComponent");
    const bool ok=attach.Set("Parent",parent).Set("SocketName",has?socket:Name{}).Set("LocationRule",rule).Set("RotationRule",rule).Set("ScaleRule",rule)
        .Set("bWeldSimulatedBodies",false).Invoke()&&attach.Get<bool>("ReturnValue");
    if(++moved_effects_<=12)Logf("NPC_SKILL effect %s %s socket=%s -> %s%s",ok?"moved":"not moved",Text(effect).c_str(),Text(socket).c_str(),Text(parent).c_str(),
        has||socket.None()?"":" (no such socket: world placement kept)");
}
void MoveSkillBullet(Object* root);
Object* Owner(Object* component);
// Spawned effects (SpawnedEffect) MoveWeaponEffects took off a hero weapon: the component and socket they came from.
// A weapon buff outlives the boss move, and the boss weapons it went onto are destroyed at the move's end: before
// 11-12 it stayed behind where that weapon had been, or went with it, and the hero's weapon came back without it.
struct MovedEffect { Identity effect,parent;Name socket; };
std::vector<MovedEffect> moved_off_weapons_;
void NoteMovedOffWeapon(Object* effect,Object* holder) {
    if(!SpawnedEffect(effect)||moved_off_weapons_.size()>=64||
       std::any_of(moved_off_weapons_.begin(),moved_off_weapons_.end(),[&](const MovedEffect& m){return m.effect.object==effect;}))return;
    Call get(effect,"GetAttachSocketName");
    moved_off_weapons_.push_back({Id(effect),Id(holder),get.Invoke()?get.Get<Name>("ReturnValue"):Name{}});
}
// The move's end, before its boss weapons go: each moved effect still playing (a buff; the move's own trails were ended
// with the ability) goes back onto the hero weapon part it came from, at its socket and its own offset (the attach
// keeps the relative transform it never lost), so it follows the hero's weapon again until the next boss move.
void ReturnWeaponEffects() {
    size_t back=0,done=0,gone=0;
    for(const MovedEffect& m:moved_off_weapons_){
        if(!Same(m.effect)||!Same(m.parent)){++gone;continue;}
        Call active(m.effect.object,"IsActive");
        if(!active.Invoke()||!active.Get<bool>("ReturnValue")){++done;continue;}  // finishing: left to fade where it is
        const bool has=HasSocket(m.parent.object,m.socket);
        Call attach(m.effect.object,"K2_AttachToComponent");
        const bool ok=attach.Set("Parent",m.parent.object).Set("SocketName",has?m.socket:Name{}).Set("LocationRule",uint8_t{0}).Set("RotationRule",uint8_t{0})
            .Set("ScaleRule",uint8_t{0}).Set("bWeldSimulatedBodies",false).Invoke()&&attach.Get<bool>("ReturnValue");
        if(ok)++back;
        if(back<=8||!ok)Logf("NPC_SKILL effect %s %s back on hero weapon %s socket=%s",ok?"put":"not put",Text(m.effect.object).c_str(),Text(m.parent.object).c_str(),
                             has?Text(m.socket).c_str():"- (root)");
    }
    if(!moved_off_weapons_.empty())Logf("NPC_SKILL weapon effects back on the hero weapons=%zu finished=%zu gone=%zu of %zu",back,done,gone,moved_off_weapons_.size());
    moved_off_weapons_.clear();
}
// Effects new under the hidden hero weapons go to the boss weapon each stands for; under the stand-in host (the hero
// weapon an effect lookup of a stand-in's index was answered with) to boss weapon row fx_row_ while that is set.
void MoveWeaponEffects() {
    if(!WeaponDisplay())return;
    for(const HiddenWeapon& w:hidden_weapons_){
        if(!Same(w.actor))continue;
        const size_t boss=fx_row_>=0&&w.actor.object==stand_in_host_.object?static_cast<size_t>(fx_row_):w.boss;
        Object* target=boss<boss_weapon_rows_.size()&&Same(boss_weapon_rows_[boss])?boss_weapon_rows_[boss].object:nullptr;
        if(!target)for(const Identity& row:boss_weapon_rows_)if(Same(row)){target=row.object;break;}
        std::vector<Object*> holders;if(Object* root=Property<Object*>(w.actor.object,"RootComponent");ue::Alive(root))holders.push_back(root);
        for(const HiddenPart& p:w.parts)if(Same(p.component)&&!IsEffect(p.component.object))holders.push_back(p.component.object);
        for(Object* holder:holders)for(Object* c:Children(holder)){
            if(!IsEffect(c)||Seen(c,holder)||std::any_of(w.parts.begin(),w.parts.end(),[&](const HiddenPart& p){return p.component.object==c;}))continue;
            if(target){NoteMovedOffWeapon(c,holder);MoveEffect(c,holder,target,false);continue;}
            // An unarmed boss (Yuu, 雷荡): an effect on a socket the NPC body has goes there (雷荡's WEAPON_R blade trail);
            // one on a socket only the hero's blade has (BladeBloodStart blood trail) the boss never draws: turned off;
            // one on the weapon root stays at the hero's hand (fist effects).
            Call get(c,"GetAttachSocketName");const Name socket=get.Invoke()?get.Get<Name>("ReturnValue"):Name{};
            if(!socket.None()&&SocketHost(socket,false)){NoteMovedOffWeapon(c,holder);MoveEffect(c,holder,WeaponDisplay(),true);continue;}
            seen_effects_.push_back({Id(c),holder});if(socket.None())continue;
            const bool off=Call(c,"Deactivate").Invoke();
            if(++unplaced_effects_<=12)Logf("NPC_SKILL effect %s on hero weapon socket %s: the unarmed boss has no such socket; deactivated=%d",Text(c).c_str(),Text(socket).c_str(),off?1:0);
        }
    }
}
void MoveSkillEffects() {
    if(!fx_live_.load(std::memory_order_relaxed)||skill_.handle<=0||!WeaponDisplay())return;
    HoldHeroWeapon(false);MoveWeaponEffects();fx_row_=-1;
    if(WeaponSource()!=nullptr)for(Object* c:Children(WeaponSource()))if(IsEffect(c)&&!Seen(c,WeaponSource()))MoveEffect(c,WeaponSource(),WeaponDisplay(),true);
    if(WeaponSource()!=nullptr)for(Object* c:Children(WeaponSource()))if(!IsEffect(c)&&!Seen(c,WeaponSource()))MoveSkillBullet(c);
}
// A bullet of the hero's that hangs on a named socket of the hero's mesh (花千笑's wine jar, BD_JiuTan on weapon_l):
// the grip hook maps the hand names as for a hero weapon (the hero's hand frame, times the weapon scale), so the
// jar sat off the hand and 1.29x too big. Its root goes onto the NPC body's own socket: offset as authored for the
// boss, scale the body's. Anything else under the mesh (hero weapons, parts) is left alone.
void MoveSkillBullet(Object* root) {
    Object* bullet=Owner(root);
    if(!ue::Alive(bullet)||Property<Object*>(bullet,"RootComponent")!=root||Property<Object*>(bullet,"BulletOwnerAvatar")!=WeaponPawn())return;
    seen_effects_.push_back({Id(root),WeaponSource()}); // looked at once, moved or not
    Call get(root,"GetAttachSocketName");const Name socket=get.Invoke()?get.Get<Name>("ReturnValue"):Name{};
    Object* host=socket.None()?nullptr:SocketHost(socket,false);Transform before;const bool known=TransformOf(root,before);
    Call attach(root,"K2_AttachToComponent");
    const bool ok=host&&attach.Set("Parent",host).Set("SocketName",socket).Set("LocationRule",uint8_t{0}).Set("RotationRule",uint8_t{0}).Set("ScaleRule",uint8_t{2})
        .Set("bWeldSimulatedBodies",false).Invoke()&&attach.Get<bool>("ReturnValue");
    if(++moved_effects_<=12)Logf("NPC_SKILL bullet %s %s socket=%s -> %s relative=(%.1f,%.1f,%.1f) scale=%.3f",ok?"moved":"left",Text(bullet).c_str(),Text(socket).c_str(),
        host?Text(host).c_str():"- (no such socket on the NPC body or a boss weapon)",known?before.translation[0]:0.0,known?before.translation[1]:0.0,known?before.translation[2]:0.0,known?before.scale[0]:0.0);
}
// ---- Boss weapon effects the hero's weapon cannot give --------------------------------------------
// The natives the boss skills' blueprint actions call get a wrapper exec in their UFunction (found by name, the slot
// proven by reflection; no code address pinned). Each runs the game's own exec first and changes only its answer, only
// while a skin's boss skill runs (fx_live_) and only for the blueprint actions concerned:
//  - WeaponUtils.GetWeaponActorsByDisplayIndex from a timeline action (LGAAction): display index >= 1 is a stand-in
//    (AddStandIns), a plain actor without the SocketComponentHolder the weapon effect actions cast to, so effects of
//    a boss's second / third weapon never spawned (lint fx-index-gap, 146 skills). The hero weapon of index 0 is
//    answered instead and what spawns on it before the next lookup goes to boss weapon row k (MoveWeaponEffects).
//  - BlueprintMapLibrary.Map_Find from GAA_PBZWeaponTrail: the trail looks its tag up in the Trails map of the avatar's
//    current Main weapon config, on a skin the hero's own (Normal.A/B/C and Kick.L/R only), so boss trails found
//    nothing or drew the hero's (weapon-trail-config, 463 skills). The answer becomes the skin's boss weapon configs'
//    (kNpcTrailConfigSets): their entry for the tag, or none as on the boss.
//  - FactionUtils.GetFactionRelation from GAAction_PowerAttackParticle / GAAction_WeaponParticle_BlueRedEye (366 skills):
//    they play only on an avatar hostile to the player (2), never the hero; answered hostile.
using NativeExec=void(*)(Object* context,unsigned char* stack,void* result);
// UFunction (UE 5.8): FunctionFlags +0x100 (FUNC_Native 0x400, FUNC_Delegate 0x100000), Func +0x128 (ParmsSize
// +0x106 as ue_runtime reads it). FFrame: Object +0x18, MostRecentProperty +0x30, MostRecentPropertyAddress +0x38.
constexpr size_t kFuncFlags=0x100,kFuncExec=0x128,kFrameObject=0x18,kFrameProperty=0x30,kFrameAddress=0x38;
constexpr uint32_t kFuncNative=0x400,kFuncDelegate=0x100000;
NativeExec game_faction_=nullptr,game_lookup_=nullptr,game_map_find_=nullptr,game_postprocess_=nullptr;
size_t trace_redirects_=0;
bool exec_hooks_tried_=false;
Object* LoadAssetPath(const char* path);
bool PinAsset(Object* asset,std::vector<Identity>& pins);
// Inside the game module's executable sections.
bool InGameCode(uintptr_t address) {
    static uintptr_t begin=0,end=0;
    if(!end&&ue::Base()){
        const uintptr_t base=ue::Base();const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+static_cast<uintptr_t>(dos->e_lfanew));const IMAGE_SECTION_HEADER* section=IMAGE_FIRST_SECTION(nt);
        for(WORD i=0;i<nt->FileHeader.NumberOfSections;++i)if(section[i].Characteristics&IMAGE_SCN_MEM_EXECUTE){
            const uintptr_t from=base+section[i].VirtualAddress,to=from+section[i].Misc.VirtualSize;if(!begin||from<begin)begin=from;if(to>end)end=to;}
    }
    return address&&address>=begin&&address<end;
}
// UObject::ProcessInternal, the exec every blueprint function holds in the Func slot: the value most script functions of
// the hero pawn's classes share there (at least three of them, three quarters agreeing). 0: the slot is not where read.
uintptr_t ScriptExec(Object* pawn) {
    Object* function_class=Class("/Script/CoreUObject.Function");std::map<uintptr_t,size_t> votes;size_t seen=0;
    for(Object* c=ue::Alive(pawn)?ue::ClassOf(pawn):nullptr;c&&function_class&&seen<32;c=ue::At<Object*>(c,ue::layout::kSuper))
        for(Object* f=ue::At<Object*>(c,ue::layout::kChildren);f&&seen<32;f=ue::At<Object*>(f,ue::layout::kFieldNext)){
            if(!ue::IsA(f,function_class)||(ue::At<uint32_t>(f,kFuncFlags)&(kFuncNative|kFuncDelegate)))continue;
            ++votes[ue::At<uintptr_t>(f,kFuncExec)];++seen;}
    const auto best=std::max_element(votes.begin(),votes.end(),[](const auto& a,const auto& b){return a.second<b.second;});
    return best!=votes.end()&&best->second>=3&&best->second*4>=seen*3&&InGameCode(best->first)?best->first:0;
}
// The last parameter before the return value is `last` (nullptr: any) and the return value is `result` bytes (0: none).
bool ParamsMatch(Object* function,const char* last,int32_t result) {
    std::string previous;int32_t result_size=0;
    for(Object* p=ue::At<Object*>(function,ue::layout::kChildProperties);p;p=ue::At<Object*>(p,ue::layout::kPropNext)){
        const Name name=ue::At<Name>(p,ue::layout::kPropName);
        if(ue::NameIs(name,"ReturnValue"))result_size=ue::At<int32_t>(p,ue::layout::kPropElementSize);else previous=Text(name);}
    return (!last||previous==last)&&result_size==result;
}
void ExecFaction(Object* context,unsigned char* stack,void* result);
void ExecWeaponLookup(Object* context,unsigned char* stack,void* result);
void ExecTrailFind(Object* context,unsigned char* stack,void* result);
void ExecPostProcess(Object* context,unsigned char* stack,void* result);
struct ExecHook { const char* cls;const char* function;const char* last;int32_t result;NativeExec ours;NativeExec* game; };
// last: read back through MostRecentPropertyAddress, so it must be the parameter the game's exec reads last.
constexpr ExecHook kExecHooks[]={
    {"/Script/SGameCombatSystem.FactionUtils","GetFactionRelation",nullptr,1,&ExecFaction,&game_faction_},
    {"/Script/SGameCombatSystem.WeaponUtils","GetWeaponActorsByDisplayIndex","OutActors",0,&ExecWeaponLookup,&game_lookup_},
    {"/Script/Engine.BlueprintMapLibrary","Map_Find","Value",1,&ExecTrailFind,&game_map_find_},
    {"/Script/Engine.SkeletalMeshComponent","GetPostProcessInstance",nullptr,8,&ExecPostProcess,&game_postprocess_},
};
// Once, on the game thread before the first skin skill: each native's slot holds a code address of the game's other
// than the script exec, FUNC_Native is set and the parameters are as read; only then is the wrapper written in.
void InstallExecHooks(Object* pawn) {
    if(exec_hooks_tried_)return;exec_hooks_tried_=true;
    const uintptr_t script=ScriptExec(pawn);std::string text;
    for(const ExecHook& hook:kExecHooks){
        Object* cls=ue::FindObject(hook.cls);Object* function=cls?ue::FindFunction(cls,hook.function):nullptr;const char* state="missing";
        if(function){
            uintptr_t& slot=ue::At<uintptr_t>(function,kFuncExec);const uintptr_t ours=reinterpret_cast<uintptr_t>(hook.ours);
            if(slot==ours)state="on";
            else if(!script)state="no-script-exec";
            else if(!(ue::At<uint32_t>(function,kFuncFlags)&kFuncNative)||!InGameCode(slot)||slot==script)state="layout";
            else if(!ParamsMatch(function,hook.last,hook.result))state="params";
            else{*hook.game=reinterpret_cast<NativeExec>(slot);slot=ours;state="on";}
        }
        text+=std::string(" ")+hook.function+"="+state;
    }
    Logf("NPC_SKILL boss fx execs:%s",text.c_str());
}
// The blueprint running the call (FFrame::Object) has a class (or parent) whose name starts with one of prefixes.
bool CallerNamed(unsigned char* stack,std::initializer_list<const char*> prefixes) {
    Object* caller=ue::At<Object*>(stack,kFrameObject);if(!ue::Alive(caller))return false;
    char text[128]{};size_t depth=0;
    for(Object* c=ue::ClassOf(caller);c&&depth<12;c=ue::At<Object*>(c,ue::layout::kSuper),++depth){
        ue::NameText(ue::NameOf(c),text,sizeof(text));
        for(const char* prefix:prefixes)if(!std::strncmp(text,prefix,std::strlen(prefix)))return true;}
    return false;
}
void ExecFaction(Object* context,unsigned char* stack,void* result) {
    game_faction_(context,stack,result);
    if(!fx_live_.load(std::memory_order_relaxed)||!result||*static_cast<uint8_t*>(result)==2)return;
    if(CallerNamed(stack,{"GAAction_PowerAttackParticle","GAAction_WeaponParticle_BlueRedEye"})){*static_cast<uint8_t*>(result)=2;++faction_forced_;}
}
Object* WeaponTracePostProcess() {
    Object* mesh=WeaponDisplay();
    Object* pp=mesh?Property<Object*>(mesh,"PostProcessAnimInstance"):nullptr;
    // A bare hero or another body's post-process may lack the boss's weapon-trace interface.
    // Leave the native answer in that case; never substitute an incompatible anim instance.
    if(!ue::Alive(pp))return nullptr;
    Object* contract=Class("/HuaShira/Interface/BPI_EnemyWeaponTrace.BPI_EnemyWeaponTrace_C");
    Call implements(ue::Default("/Script/Engine.Default__KismetSystemLibrary"),"DoesImplementInterface");
    const bool compatible=contract&&implements.Set("TestObject",pp).Set("Interface",contract).Invoke()&&implements.Get<bool>("ReturnValue");
    return NpcSkillSupport::TraceReceiver(compatible,ue::FindFunction(ue::ClassOf(pp),"SetTraceTarget")!=nullptr)?pp:nullptr;
}
void ExecPostProcess(Object* context,unsigned char* stack,void* result) {
    game_postprocess_(context,stack,result);
    if(!result||!fx_live_.load(std::memory_order_relaxed)||context!=WeaponSource())return;
    Object* pp=WeaponTracePostProcess();
    if(!NpcSkillSupport::RedirectTrace(true,true,CallerNamed(stack,{"GA_WeaponTraceAttack_Base"}),pp!=nullptr))return;
    *static_cast<Object**>(result)=pp;
    if(++trace_redirects_<=4)Logf("NPC_SKILL weapon trace initial PP -> %s (before first SetTraceTarget)",Text(pp).c_str());
}
// An effect lookup that found the hero's stand-ins: each becomes the stand-in host (once), and the effects that spawn
// on it from now go to the boss weapon row of the index asked for. Anything the earlier lookups spawned moves first.
void RedirectEffectLookup(Array& out,Object* caller) {
    if(out.num<=0||out.num>16||!FaceReadable(out.data,static_cast<size_t>(out.num)*sizeof(Object*)))return;
    Object* host=Same(stand_in_host_)?stand_in_host_.object:nullptr;
    const NpcFxLookup::Result r=NpcFxLookup::Rewrite(static_cast<Object**>(out.data),out.num,host,[&](Object* actor){
        const auto s=std::find_if(stand_ins_.begin(),stand_ins_.end(),[&](const StandIn& x){return x.actor.object==actor;});return s!=stand_ins_.end()?s->index:-1;});
    if(!r.related)return;
    MoveWeaponEffects(); // what the earlier lookups' effects spawned goes by their row first
    fx_row_=r.only?r.row:-1;out.num=r.num;
    if(r.row<0||!host)return;
    if(++fx_redirects_<=8)Logf("NPC_SKILL effect lookup by %s: display %d -> hero weapon %s, effects to boss weapon row %d",
        Text(ue::ClassOf(caller)).c_str(),r.row,Text(host).c_str(),fx_row_);
}
void ExecWeaponLookup(Object* context,unsigned char* stack,void* result) {
    game_lookup_(context,stack,result);
    if(!fx_live_.load(std::memory_order_relaxed)||stand_ins_.empty())return;
    static Object* const action=Class("/Script/LegoAbility.LGAAction");
    Object* caller=ue::At<Object*>(stack,kFrameObject);Array* out=ue::At<Array*>(stack,kFrameAddress);
    if(!action||!ue::Alive(caller)||!ue::IsA(caller,action)||!out||!FaceReadable(out,sizeof(Array)))return;
    RedirectEffectLookup(*out,caller);
}
// A TMap<FGameplayTag or FName, V> property's value for key (layout from reflection, read as FindMainSlot reads one).
const unsigned char* MapValue(Object* object,const char* property,Name key) {
    const uintptr_t field=object?Field(ue::ClassOf(object),property):0;Object* map=reinterpret_cast<Object*>(field);
    if(!field||FieldKind(map)!=0x400000000000ull||key.None())return nullptr;
    Object* key_property=ue::At<Object*>(map,0x70);if(!key_property||ue::At<int32_t>(key_property,ue::layout::kPropElementSize)!=8)return nullptr;
    const int32_t value_offset=ue::At<int32_t>(map,0x80),stride=ue::At<int32_t>(map,0x8c);
    const unsigned char* at=reinterpret_cast<const unsigned char*>(object)+ue::At<int32_t>(map,ue::layout::kPropOffset);
    const int32_t count=*reinterpret_cast<const int32_t*>(at+8),bits=*reinterpret_cast<const int32_t*>(at+40);
    const unsigned char* data=*reinterpret_cast<unsigned char* const*>(at);const uint32_t* allocation=*reinterpret_cast<uint32_t* const*>(at+32);
    if(!allocation)allocation=reinterpret_cast<const uint32_t*>(at+16);
    if(stride<16||stride>4096||value_offset<8||value_offset>=stride||count<=0||count>1024||bits<count||
        !FaceReadable(data,static_cast<size_t>(count)*static_cast<size_t>(stride))||!FaceReadable(allocation,(static_cast<size_t>(count)+31)/32*4))return nullptr;
    for(int32_t i=0;i<count;++i){const unsigned char* row=data+static_cast<size_t>(i)*static_cast<size_t>(stride);
        if((allocation[i/32]&(1u<<(i%32)))&&*reinterpret_cast<const Name*>(row)==key)return row+value_offset;}
    return nullptr;
}
// Every member of the struct value is a plain value, or a string / array / soft path that is empty here (its header
// only): a byte copy of the whole struct is then what the property copy does.
std::string bytewise_block_; // the field BytewiseCopyable last stopped at (name:kind), for the log
bool BytewiseCopyable(Object* type,const unsigned char* at,int depth=0) {
    if(depth>4||!ue::Alive(type))return false;
    for(Object* s=type;s;s=ue::At<Object*>(s,ue::layout::kSuper))
        for(Object* p=ue::At<Object*>(s,ue::layout::kChildProperties);p;p=ue::At<Object*>(p,ue::layout::kPropNext)){
            const unsigned char* value=at+ue::At<int32_t>(p,ue::layout::kPropOffset);
            const uint64_t kind=FieldKind(p);
            const auto block=[&]{char text[32]{};_snprintf_s(text,sizeof(text),_TRUNCATE,":%llx",static_cast<unsigned long long>(kind));
                bytewise_block_=Text(ue::At<Name>(p,ue::layout::kPropName))+text;return false;};
            switch(kind){
            case 0x20000:case 0x40:case 0x2:case 0x80000000:case 0x40000:case 0x80:case 0x800:case 0x400000:case 0x200:case 0x1000000000000ull:
            case 0x100:case 0x100000000ull:case 0x2000:case 0x10000:case 0x400:case 0x1000:case 0x8000000:continue;
            case 0x100000:if(!BytewiseCopyable(ue::At<Object*>(p,0x70),value,depth+1))return false;continue;
            case 0x4000:case 0x200000:if(reinterpret_cast<const Array*>(value)->data)return block();continue;
            case 0x20000000:case 0x200000000ull:if(reinterpret_cast<const SoftPath*>(value)->sub.data)return block();continue;
            default:return block();
            }
        }
    return true;
}
bool CopyBytewise(Object* type,unsigned char* to,const unsigned char* from) {
    const int32_t size=ue::StructSize(type);
    if(size<=0||size>4096||!BytewiseCopyable(type,to)||!BytewiseCopyable(type,from))return false;
    std::memcpy(to,from,static_cast<size_t>(size));return true;
}
// A value with filled arrays (影魅's NS float parameters, 梦华's TagRequirementTrails): members CopyMembers can
// copy are plain ones, structs of them and arrays of anything (the game copies their elements). Maps, sets,
// strings and soft paths that hold text are not.
bool MembersCopyable(Object* type,const unsigned char* at,int depth=0) {
    if(depth>4||!ue::Alive(type))return false;
    for(Object* s=type;s;s=ue::At<Object*>(s,ue::layout::kSuper))
        for(Object* p=ue::At<Object*>(s,ue::layout::kChildProperties);p;p=ue::At<Object*>(p,ue::layout::kPropNext)){
            const unsigned char* value=at+ue::At<int32_t>(p,ue::layout::kPropOffset);
            switch(FieldKind(p)){
            case 0x20000:case 0x40:case 0x2:case 0x80000000:case 0x40000:case 0x80:case 0x800:case 0x400000:case 0x200:case 0x1000000000000ull:
            case 0x100:case 0x100000000ull:case 0x2000:case 0x10000:case 0x400:case 0x1000:case 0x8000000:continue;
            case 0x100000:if(!MembersCopyable(ue::At<Object*>(p,0x70),value,depth+1))return false;continue;
            case 0x200000:{Object* inner=ue::At<Object*>(p,0x78);const Array list=*reinterpret_cast<const Array*>(value);
                const int32_t stride=inner?ue::At<int32_t>(inner,ue::layout::kPropElementSize):0;
                if(stride<=0||stride>4096||list.num<0||list.num>256||list.max<list.num||(list.num&&!FaceReadable(list.data,size_t(list.num)*size_t(stride))))return false;continue;}
            case 0x4000:if(reinterpret_cast<const Array*>(value)->data)return false;continue;
            case 0x20000000:case 0x200000000ull:if(reinterpret_cast<const SoftPath*>(value)->sub.data)return false;continue;
            default:return false;
            }
        }
    return true;
}
// from onto to member by member (both MembersCopyable): plain members bytewise, structs member by member, arrays
// emptied and refilled through the game's own GenericArray_Remove / GenericArray_Add, which free and copy each
// element with its property (nested containers included), so the engine frees them as it frees its own.
void CopyMembers(Object* type,unsigned char* to,const unsigned char* from,int depth=0) {
    using AddFn=int32_t(__fastcall*)(void*,const void*,const void*);using RemoveFn=void(__fastcall*)(void*,const void*,int32_t);
    const AddFn add=reinterpret_cast<AddFn>(ue::Base()+kArrayAddRva);const RemoveFn remove=reinterpret_cast<RemoveFn>(ue::Base()+kArrayRemoveRva);
    for(Object* s=type;ue::Alive(s);s=ue::At<Object*>(s,ue::layout::kSuper))
        for(Object* p=ue::At<Object*>(s,ue::layout::kChildProperties);p;p=ue::At<Object*>(p,ue::layout::kPropNext)){
            const int32_t off=ue::At<int32_t>(p,ue::layout::kPropOffset);unsigned char* t=to+off;const unsigned char* f=from+off;const uint64_t kind=FieldKind(p);
            if(kind==0x100000){if(depth<4)CopyMembers(ue::At<Object*>(p,0x70),t,f,depth+1);continue;}
            if(kind==0x200000){
                Array* list=reinterpret_cast<Array*>(t);const Array source=*reinterpret_cast<const Array*>(f);
                const int32_t stride=ue::At<int32_t>(ue::At<Object*>(p,0x78),ue::layout::kPropElementSize);
                for(int32_t i=list->num-1;i>=0;--i)remove(list,p,i);
                for(int32_t i=0;i<source.num;++i)add(list,p,static_cast<const unsigned char*>(source.data)+size_t(i)*size_t(stride));
                continue;}
            const int32_t dim=(std::max)(ue::At<int32_t>(p,0x30),1),size=ue::At<int32_t>(p,ue::layout::kPropElementSize);
            if(size>0&&size<=4096&&dim<=64)std::memmove(t,f,size_t(size)*size_t(dim));
        }
}
// The trail action's tag ("Get Trail Tag": Trail Tag, or Reinforced Trail Tag under 煞气) in the skin's boss weapon
// configs: the first one that has it lends its entry; none has it: not found (the boss draws nothing either).
void LendTrail(Object* caller,unsigned char* value,bool& found) {
    Call get(caller,"Get Trail Tag");if(!get.Set("Preview",false).Invoke())return;
    const Name tag=get.Get<Name>("ReturnValue");if(tag.None())return;
    for(const Identity& config:trail_configs_){
        const unsigned char* data=Same(config)?MapValue(config.object,"Trails",tag):nullptr;if(!data)continue;
        bytewise_block_.clear();const char* how="bytewise";
        if(!CopyBytewise(trail_struct_,value,data)){
            if(!NativeHelpersMatch()||!MembersCopyable(trail_struct_,value)||!MembersCopyable(trail_struct_,data)){
                if(++trails_refused_<=8)Logf("NPC_SKILL trail %s in %s: not a plain value (%s); the hero's answer kept",Text(tag).c_str(),Text(config.object).c_str(),bytewise_block_.empty()?"-":bytewise_block_.c_str());return;}
            CopyMembers(trail_struct_,value,data);how=bytewise_block_.c_str();
        }
        found=true;if(++trails_lent_<=8)Logf("NPC_SKILL trail %s from %s (%s)",Text(tag).c_str(),Text(config.object).c_str(),how);return;
    }
    if(!trail_configs_.empty()&&found){found=false;if(++trails_refused_<=8)Logf("NPC_SKILL trail %s: on none of the skin's boss weapon configs; not drawn, as on the boss",Text(tag).c_str());}
}
void ExecTrailFind(Object* context,unsigned char* stack,void* result) {
    game_map_find_(context,stack,result);
    if(!fx_live_.load(std::memory_order_relaxed)||!result||!trail_struct_)return;
    Object* property=ue::At<Object*>(stack,kFrameProperty);unsigned char* value=ue::At<unsigned char*>(stack,kFrameAddress);
    if(!property||!value||FieldKind(property)!=0x100000||ue::At<Object*>(property,0x70)!=trail_struct_)return;
    if(!CallerNamed(stack,{"GAA_PBZWeaponTrail","GAA_PBZJianChiWeaponTrail"}))return;
    LendTrail(ue::At<Object*>(stack,kFrameObject),value,*static_cast<bool*>(result));
}
// The skin's boss weapon configs, loaded and held (skill_pins_) at its first skill; trail_struct_ from the first.
void LoadTrailConfigs() {
    const int32_t skin=WeaponSkin();
    if(skin<0||static_cast<size_t>(skin)>=std::size(kNpcTrailConfigSets))return;
    auto& pins=bare_weapons_?bare_pins_:skill_pins_;
    if(trail_configs_for_==skin&&std::all_of(trail_configs_.begin(),trail_configs_.end(),[&](const Identity& c){
        return Same(c)&&std::any_of(pins.begin(),pins.end(),[&](const Identity& p){return p.object==c.object&&Same(p);});}))return;
    trail_configs_.clear();trail_struct_=nullptr;trail_configs_for_=skin;
    const NpcTrailConfigs& row=kNpcTrailConfigSets[skin];
    for(size_t i=0;i<row.count;++i){
        Object* config=LoadAssetPath(row.configs[i]);if(!config)continue;
        if(std::none_of(pins.begin(),pins.end(),[&](const Identity& p){return p.object==config&&Same(p);})&&!PinAsset(config,pins))continue;
        trail_configs_.push_back(Id(config));
        if(trail_struct_)continue;
        Object* map=reinterpret_cast<Object*>(Field(ue::ClassOf(config),"Trails"));Object* value=map&&FieldKind(map)==0x400000000000ull?ue::At<Object*>(map,0x78):nullptr;
        if(value&&FieldKind(value)==0x100000)trail_struct_=ue::At<Object*>(value,0x70);
    }
    if(row.count)Logf("NPC_SKILL boss weapon configs for trails %zu/%zu (%s)",trail_configs_.size(),row.count,trail_struct_?Text(trail_struct_).c_str():"no Trails map");
}
// ---- Collider tags the cast skill's sweeps look for ------------------------------------------------
// kNpcColliderTagSets come from the boss blueprints offline. A borrowed move (another boss's skill on the skin), an
// avatar sweep on a tag the boss body carries under a socket shape the table missed, or a weapon index past the boss's
// weapons can still sweep for a tag nothing on its source has (no damage; lint tag-uncopied / tag-not-on-boss). At
// the cast the sweep bullet data on the ability's timelines are read (OriginType 0 avatar / 1 the weapons of
// WeaponIndex, ColliderData.ComponentTag), and every source still missing its tag gets shapes with it: the boss's own
// (weapon rows for a weapon source, the body first for the avatar), else its own attack shapes copied under that tag.
struct SweepNeed { uint8_t origin=0;int32_t index=0;Name tag; };
bool ClassNameHas(Object* object,const char* needle) {
    char text[128]{};size_t depth=0;
    for(Object* c=ue::Alive(object)?ue::ClassOf(object):nullptr;c&&depth<12;c=ue::At<Object*>(c,ue::layout::kSuper),++depth){
        ue::NameText(ue::NameOf(c),text,sizeof(text));if(std::strstr(text,needle))return true;}
    return false;
}
void SweepNeedOf(Object* data,std::vector<SweepNeed>& out) {
    Object* collider=reinterpret_cast<Object*>(Field(ue::ClassOf(data),"ColliderData"));Object* type=collider&&FieldKind(collider)==0x100000?ue::At<Object*>(collider,0x70):nullptr;
    Object* existing=type?reinterpret_cast<Object*>(Field(type,"IsExisting")):nullptr;const int32_t tag_at=type?ue::Offset(type,"ComponentTag"):-1;
    const int32_t origin_at=Offset(data,"OriginType"),index_at=Offset(data,"WeaponIndex");
    if(!existing||FieldKind(existing)!=0x20000||tag_at<0||origin_at<0||index_at<0)return;
    const unsigned char* c=reinterpret_cast<const unsigned char*>(data)+ue::At<int32_t>(collider,ue::layout::kPropOffset);
    if(!(c[ue::At<int32_t>(existing,ue::layout::kPropOffset)+ue::At<uint8_t>(existing,0x71)]&ue::At<uint8_t>(existing,0x73)))return;
    SweepNeed need;need.origin=ue::At<uint8_t>(data,origin_at);need.index=ue::At<int32_t>(data,index_at);need.tag=*reinterpret_cast<const Name*>(c+tag_at);
    if(need.tag.None()||std::any_of(out.begin(),out.end(),[&](const SweepNeed& n){return n.origin==need.origin&&n.index==need.index&&n.tag==need.tag;}))return;
    if(out.size()<64)out.push_back(need);
}
// Sweep bullet data an object's fields name: directly, in arrays, in structs (two levels).
void FindSweeps(Object* type,const unsigned char* base,int depth,std::vector<SweepNeed>& out,std::vector<Object*>& seen) {
    const auto object=[&](Object* o){
        if(!ue::Alive(o)||std::find(seen.begin(),seen.end(),o)!=seen.end()||seen.size()>=512)return;
        seen.push_back(o);if(ClassNameHas(o,"SweepTraceBulletData"))SweepNeedOf(o,out);};
    for(Object* s=type;ue::Alive(s);s=ue::At<Object*>(s,ue::layout::kSuper))
        for(Object* p=ue::At<Object*>(s,ue::layout::kChildProperties);p;p=ue::At<Object*>(p,ue::layout::kPropNext)){
            const unsigned char* at=base+ue::At<int32_t>(p,ue::layout::kPropOffset);const uint64_t kind=FieldKind(p);
            if(kind==0x10000)object(*reinterpret_cast<Object* const*>(at));
            else if(kind==0x100000&&depth<2)FindSweeps(ue::At<Object*>(p,0x70),at,depth+1,out,seen);
            else if(kind==0x200000){
                Object* inner=ue::At<Object*>(p,0x78);const Array list=*reinterpret_cast<const Array*>(at);const uint64_t inner_kind=inner?FieldKind(inner):0;
                const int32_t stride=inner?ue::At<int32_t>(inner,ue::layout::kPropElementSize):0;
                if(list.num<=0||list.num>256||stride<=0||!FaceReadable(list.data,static_cast<size_t>(list.num)*static_cast<size_t>(stride)))continue;
                for(int32_t i=0;i<list.num;++i){const unsigned char* item=static_cast<const unsigned char*>(list.data)+static_cast<size_t>(i)*static_cast<size_t>(stride);
                    if(inner_kind==0x10000)object(*reinterpret_cast<Object* const*>(item));
                    else if(inner_kind==0x100000&&depth<2)FindSweeps(ue::At<Object*>(inner,0x70),item,depth+1,out,seen);}
            }
        }
}
// The sweeps of the ability (its fields and�my��$z{-���jםeparation_status.c_str()).c_str());
            ++audit_.grab;audit_.phase=P::GrabNext;}
        return;}
    case P::GrabTrigger:{
        if(now<audit_.at||!ok)return;
        const MotianGrab::Snapshot s=MotianGrab::GetSnapshot();
        audit_.grab_attempts=s.attempts;audit_.grab_activations=s.activations;audit_.grab_hits=s.paired_hits;audit_.grab_active=audit_.grab_paired=false;
        DamageTarget target;audit_.grab_health=DamageCandidate(audit_.dummy.object,context.pawn,target)?target.last:-1;
        Logf("NPC_AUDIT_GRAB_START pos=%zu variant=%d dummyHealth=%.0f",audit_.grab,audit_.grab_index,audit_.grab_health);
        MotianGrab::RequestTrigger(audit_.grab_index,false);audit_.at=now;audit_.deadline=now+25000;audit_.phase=P::GrabRun;return;}
    case P::GrabRun:{
        const MotianGrab::Snapshot s=MotianGrab::GetSnapshot();
        audit_.grab_active|=s.active;audit_.grab_paired|=s.paired;
        const bool tried=s.attempts>audit_.grab_attempts,timeout=now>audit_.deadline;
        if(!timeout&&(s.active||s.cleanup_pending||(!tried&&now<audit_.at+3000)||now<audit_.at+1500))return;
        if(timeout&&(s.active||s.cleanup_pending))MotianGrab::RequestCancel();
        float health=-1;{DamageTarget target;if(ok&&Same(audit_.dummy)&&DamageCandidate(audit_.dummy.object,context.pawn,target))health=target.last;}
        const MotianGrab::Variant* v=audit_.grab<s.variants.size()?&s.variants[audit_.grab]:nullptr;
        // Refused (its target, footing, or a wall / step on the authored pair's spot) or started without pairing: once more
        // after the dummy has stood up a while longer, then facing 90 / 180 / 270 degrees round from the start spot.
        if(!timeout&&tried&&!audit_.grab_paired&&audit_.grab_turns<4){++audit_.grab_turns;audit_.home_yaw=audit_.grab_yaw0+90.0*(audit_.grab_turns-1);
            Logf("NPC_AUDIT_GRAB_RETRY variant=%d turn=%d yaw=%.0f status=%s",audit_.grab_index,audit_.grab_turns,audit_.home_yaw,Utf8(s.status.c_str()).c_str());
            if(ok)AuditPlace(context,audit_.grab_distance,true);audit_.at=now+4000;audit_.deadline=now+34000;audit_.phase=P::GrabPrepare;return;}
        Logf("NPC_AUDIT_GRAB variant=%d id=%s boss=%s result=%s ms=%llu turns=%d attempts=+%u activations=+%u pairedHits=+%u dummyDamage=%.0f status=%s alignment=%s",audit_.grab_index,
            v?Utf8(v->id.c_str()).c_str():"-",v?Utf8(v->boss_id.c_str()).c_str():"-",timeout?"timeout":audit_.grab_paired?"paired":audit_.grab_active?"started":tried?"refused":"no-attempt",now-audit_.at,audit_.grab_turns,
            s.attempts-audit_.grab_attempts,s.activations-audit_.grab_activations,s.paired_hits-audit_.grab_hits,audit_.grab_health>=0&&health>=0?audit_.grab_health-health:-1.0f,
            Utf8(s.status.c_str()).c_str(),Utf8(s.alignment_status.c_str()).c_str());
        ++audit_.grab;++audit_.grabs_done;audit_.phase=P::GrabNext;
        if(ok)AuditPlace(context,150,true); // live, so it gets up before the next grab (GrabPrepare waits)
        audit_.at=now+4000;return;}
    case P::Skin:{
        if(audit_.profile>=audit_.end){audit_.phase=P::Finish;return;}
        const NpcSkillSet& set=kNpcSkillSets[audit_.profile];
        if(!set.skill_count){Logf("NPC_AUDIT_SKIN profile=%zu id=%s result=no-skills",audit_.profile,set.profile);AuditNextSkin();return;}
        if(active_&&selected_==static_cast<int32_t>(audit_.profile)&&!cleanup_pending_){
            if(audit_.requested)Logf("NPC_AUDIT_SKIN profile=%zu id=%s result=applied ms=%llu skills=%zu weapons=%zu",audit_.profile,set.profile,now-audit_.at,set.skill_count,set.weapon_count);
            audit_.requested=false;audit_.placed=false;++audit_.skins;audit_.phase=P::Settle;AuditInitKinds();
            for(size_t i=0;i<set.weapon_count;++i){const std::string path=set.weapons[i].actor_class;const size_t dot=path.find('.');
                if(dot==std::string::npos)continue;if(!Class(path.c_str()))LoadClassPackage(path.substr(0,dot));
                DumpWeapon("bossWeaponDefault",Class((path.substr(0,dot+1)+"Default__"+path.substr(dot+1)).c_str()));}
            return;}
        if(!audit_.requested){
            if(!ok||skill_.handle>0||pending_.handle>0||damage_.open||waiting_action_>=0)return;
            AuditPlace(context);
            waiting_action_=static_cast<int32_t>(Action::Apply);waiting_entry_=static_cast<int32_t>(audit_.profile);wait_deadline_=now+60000;
            audit_.requested=true;audit_.at=now;audit_.deadline=now+kAuditSkinMs;
            Logf("NPC_AUDIT_SKIN profile=%zu id=%s result=requested",audit_.profile,set.profile);return;}
        if((waiting_action_<0&&now>audit_.at+1500)||now>audit_.deadline){
            if(now>audit_.deadline)waiting_action_=-1;
            Logf("NPC_AUDIT_SKIN profile=%zu id=%s result=failed ms=%llu why=%s",audit_.profile,set.profile,now-audit_.at,failure_text_.empty()?"-":Utf8(failure_text_.c_str()).c_str());
            AuditNextSkin();}
        return;}
    case P::Settle:{
        if(skill_.handle>0||pending_.handle>0||damage_.open)return;
        const NpcSkillSet& set=kNpcSkillSets[audit_.profile];
        if(audit_.skill>=set.skill_count){AuditNextSkin();return;}
        if(!(active_&&selected_==static_cast<int32_t>(audit_.profile))||cleanup_pending_){audit_.requested=false;audit_.phase=P::Skin;return;}
        if(!Same(audit_.dummy)||AuditDead(audit_.dummy.object)){Logf("NPC_AUDIT dummy lost; a new one");audit_.dummy={};audit_.deadline=now+30000;audit_.phase=P::Dummy;return;}
        if(!ok)return;
        if(audit_speed_>1.01f&&audit_.phase==P::Settle&&!audit_.placed)
            SetAuditTimeDilation(context.world,audit_speed_); // every cast and its waits then run at that speed
        if(!audit_.placed){audit_.dummy_cm=audit_.close_retry?kAuditCloseCm:kAuditDummyCm;AuditPlace(context,audit_.dummy_cm);audit_.placed=true;
            audit_.at=now+static_cast<ULONGLONG>(800.0/(std::max)(1.0f,audit_speed_));return;}
        if(now<audit_.at||(!Idle(context)&&now<audit_.at+5000))return;
        AuditResetSkill();AuditScan(true);audit_.placed=false;
        Logf("NPC_AUDIT_SKILL_CAST profile=%zu id=%s skill=%zu/%zu",audit_.profile,set.profile,audit_.skill+1,set.skill_count);
        if(SelfKillMove(set.skills[audit_.skill])){audit_.result="self-kill";AuditReport();AuditAdvance();return;}  // never cast on the hero
        if(!CastExact(audit_.skill)){audit_.result="grant-failed";AuditReport();AuditAdvance();return;}
        // Scan and settle timers are game-time budgets: at 2x the skill ends in about half the wall time, so the
        // scan interval and the deadline shrink with it and keep the same game-time sampling density.
        const ULONGLONG scale=static_cast<ULONGLONG>((std::max)(1.0f,audit_speed_));
        audit_.deadline=now+kAuditSkillMs/scale;audit_.next_scan=now+100;audit_.phase=P::Run;return;}
    case P::Run:{
        const NpcSkillSet& set=kNpcSkillSets[audit_.profile];
        if(!audit_.started&&skill_.handle>0&&!audit_.hero_weapons_dumped){audit_.hero_weapons_dumped=true;
            for(size_t i=0;i<hidden_weapons_.size()&&i<3;++i)if(Same(hidden_weapons_[i].actor))DumpWeapon("heroWeapon",hidden_weapons_[i].actor.object);}
        if(!audit_.started&&skill_.handle>0){audit_.started=true;audit_.cast_at=skill_.started;
            Logf("NPC_AUDIT_SKILL_START profile=%zu id=%s skill=%zu/%zu ability=%s",audit_.profile,set.profile,audit_.skill+1,set.skill_count,skill_.name.c_str());}
        if(audit_.started&&audit_.montage.empty()&&skill_.handle>0&&now>audit_.cast_at+200&&ok)
            if(Object* anim=AnimInstance(context.mesh)){Call current(anim,"GetCurrentActiveMontage");if(current.Invoke())if(Object* m=current.Get<Object*>("ReturnValue"))audit_.montage=Text(m);}
        if(now>=audit_.next_scan){audit_.next_scan=now+(std::max)(ULONGLONG{60},static_cast<ULONGLONG>(double(kAuditScanMs)/(std::max)(1.0f,audit_speed_)));AuditScan(false);}
        if(skill_.handle>0&&now>audit_.deadline){audit_.result="timeout";ClearSkill("audit-timeout");}
        if(skill_.handle>0||pending_.handle>0||damage_.open)return;
        AuditScan(false);AuditReport();
        // Same skill, dummy closer; not in a sped-up pass, whose flagged skills the runner casts again at 1x.
        if(audit_speed_<=1.01f&&!audit_.close_retry&&audit_.result.empty()&&!audit_.bullets.empty()&&!audit_.dummy_hits){audit_.close_retry=true;audit_.phase=P::Settle;return;}
        AuditAdvance();audit_.phase=P::Settle;return;}
    case P::Finish:{
        ClearSkill("audit-end");
        RestoreAuditTimeDilation();audit_speed_=1.0f; // before anything else: the rest of the game runs at normal speed again
        if(Same(audit_.dummy)){Object* controller=Property<Object*>(audit_.dummy.object,"Controller");
            Call(audit_.dummy.object,"K2_DestroyActor").Invoke();if(ue::Alive(controller))Call(controller,"K2_DestroyActor").Invoke();}
        if(active_){waiting_action_=static_cast<int32_t>(Action::Restore);waiting_entry_=-1;wait_deadline_=now+60000;}
        if(ok)AuditTeleport(context.pawn,audit_.home,audit_.home_yaw);
        EntryProtection::ReleaseProtection();
        if(audit_.grabs&&audit_.grab_index>=0)MotianGrab::RequestOptions(audit_.grab_options);
        Logf("NPC_AUDIT done reason=%s skins=%zu skills=%zu nextSkin=%zu grabs=%zu nextGrab=%zu minutes=%.1f",audit_.stop.empty()?"complete":audit_.stop.c_str(),audit_.skins,audit_.skills,audit_.profile,
            audit_.grabs_done,audit_.grab,double(now-audit_.begun)/60000.0);
        audit_.phase=P::Off;audit_.dummy={};return;}
    default:return;
    }
}
// ---- 瞬身 (blink) around look changes -----------------------------------------------------------------
// Every look change the player or a chain asks for (the page, the hotkey, 投技时变身, BOSS 技能) happens behind the
// game's own blink: DangHui's vanish burst and the ShunYi whoosh where the hero stands, the hero and everything on it
// hidden while the look changes and kBlinkHoldMs after, then the appear burst and whoosh. Never for the clean-ups,
// the scene resume or the audit. Each burst is sized to the look the hero has at that moment (the vanish: the look it
// leaves; the appear: the look it comes back in): an NPC look's rest pelvis height over the hero's (skin_size_, 1 for
// the hero), centred on that body's middle (the feet plus the hero's half height times that size), not the capsule's
// (a big boss vanished in a hero-sized burst at its knees until 11-08).
constexpr char kBlinkOutFx[]="/PBZVFXPack/VFX/BattleFX/DangHui/NS_DangHui_Blink_301_Hide.NS_DangHui_Blink_301_Hide";
constexpr char kBlinkInFx[]="/PBZVFXPack/VFX/BattleFX/DangHui/NS_DangHui_Blink_301_Show.NS_DangHui_Blink_301_Show";
constexpr char kBlinkOutSound[]="/Game/Wwise/Events/SFX/Combat/VFX/Monster_VFX/Whoosh/SFX_Combat_VFX_Monster_Whoosh_ShunYi_Disappear.SFX_Combat_VFX_Monster_Whoosh_ShunYi_Disappear";
constexpr char kBlinkInSound[]="/Game/Wwise/Events/SFX/Combat/VFX/Monster_VFX/Whoosh/SFX_Combat_VFX_Monster_Whoosh_ShunYi_Appear.SFX_Combat_VFX_Monster_Whoosh_ShunYi_Appear";
constexpr ULONGLONG kBlinkHoldMs=180,kBlinkMaxMs=3000;
struct BlinkHidden { Identity actor; };
struct Blink { bool on=false; bool fx=true; Identity pawn; std::vector<BlinkHidden> hidden; ULONGLONG show_at=0,until=0; };
Blink blink_;
constexpr double kBlinkSizeMin=0.5,kBlinkSizeMax=4.0;
// The size of the look on `pawn` now: the NPC look's (skin_size_, times the scale 主角体型 shows it at) while it is on
// that pawn, else the hero's own (1).
double BlinkSize(Object* pawn){
    const double shown=hero_size_shown_.load(std::memory_order_relaxed),size=skin_size_*(std::isfinite(shown)&&shown>0?shown:1.0);
    return active_&&pawn==pawn_.object&&std::isfinite(size)?std::clamp(size,kBlinkSizeMin,kBlinkSizeMax):1.0;}
void BlinkFx(Object* pawn,const char* fx,const char* sound,const char* what,double size=-1){
    Call where(pawn,"K2_GetActorLocation");if(!where.Invoke())return;Vector at=where.Get<Vector>("ReturnValue");
    if(!(size>0))size=BlinkSize(pawn);
    size=std::clamp(std::isfinite(size)?size:1.0,kBlinkSizeMin,kBlinkSizeMax);
    // The capsule's middle is the hero's middle; another body's middle is the feet plus the hero's half height times its
    // size (the capsule may already be the look's, HoldBodyCollision: the feet come from the capsule as it is now).
    double lift=0;
    if(std::fabs(size-1)>0.01)if(Object* capsule=Property<Object*>(pawn,"CapsuleComponent")){
        Call half(capsule,"GetScaledCapsuleHalfHeight");
        if(half.Invoke()){const double now=half.Get<float>("ReturnValue"),hero=body_.owned&&body_.half>1?body_.half:now;
            if(std::isfinite(now)&&now>1&&std::isfinite(hero)&&hero>1)lift=hero*size-now;}}
    at.z+=lift;
    Object* system=LoadAssetPath(fx);bool shown=false;
    if(ue::Alive(system)){Call spawn(Niagara(),"SpawnSystemAtLocation");
        shown=spawn.Set("WorldContextObject",pawn).Set("SystemTemplate",system).Set("Location",at).Set("Rotation",RotatorD{}).Set("Scale",Vector{size,size,size})
            .Set("bAutoDestroy",true).Set("bAutoActivate",true).Set("PoolingMethod",uint8_t{0}).Set("bPreCullCheck",false).Invoke()&&ue::Alive(spawn.Get<Object*>("ReturnValue"));}
    Object* event=LoadAssetPath(sound);bool heard=false;
    if(ue::Alive(event)){Call post(ue::Default("/Script/AkAudio.Default__AkGameplayStatics"),"PostEventAtLocation");
        heard=post.Set("AkEvent",event).Set("Location",at).Set("Orientation",RotatorD{}).Set("WorldContextObject",pawn).Invoke();}
    Logf("NPC_BLINK %s fx=%d sound=%d size=%.2f lift=%.0fcm",what,shown?1:0,heard?1:0,size,lift);
}
// Stops what the hero is doing for a chain's look change: its running action abilities (attack, dodge, jump ...)
// cancelled and its montages stopped (0.1 s blend). Returns what was cancelled ("none": nothing ran).
std::string CancelHeroActions(const Context& context, bool montages) {
    std::string out; int stopped = 0;
    Object* asc = PawnAsc(context.pawn);
    Object* type = Class("/Script/GameplayAbilities.GameplayAbilitySpec"), *container = Class("/Script/GameplayAbilities.GameplayAbilitySpecContainer");
    const int32_t specs = ue::Alive(asc) ? Offset(asc, "ActivatableAbilities") : -1, items = container ? ue::Offset(container, "Items") : -1, size = type ? ue::StructSize(type) : 0,
        handle_at = type ? ue::Offset(type, "Handle") : -1, ability_at = type ? ue::Offset(type, "Ability") : -1, count_at = type ? ue::Offset(type, "ActiveCount") : -1;
    if (specs >= 0 && items >= 0 && size > 0 && handle_at >= 0 && ability_at >= 0 && count_at >= 0) {
        const Array array = ue::At<Array>(asc, specs + items);
        std::vector<std::pair<int32_t, Object*>> running;
        if (array.num > 0 && array.num <= 4096 && array.max >= array.num && FaceReadable(array.data, static_cast<size_t>(array.num) * static_cast<size_t>(size)))
            for (int32_t i = 0; i < array.num; ++i) {
                Object* row = static_cast<Object*>(array.data) + static_cast<size_t>(i) * static_cast<size_t>(size);
                if (!ue::At<uint8_t>(row, count_at)) continue;
                Object* ability = ue::At<Object*>(row, ability_at);
                const int32_t handle = ue::At<int32_t>(row, handle_at);
                if (!AbilityActionBits(ability) || handle == skill_.handle || handle == pending_.handle) continue;
                running.push_back({handle, ability});
            }
        for (const auto& [handle, ability] : running)  // read first: a cancel may change the spec array
            if (CancelSpec(asc,handle)) {
                std::string name = Text(ability); const size_t dot = name.find_last_of("./"); if (dot != std::string::npos) name = name.substr(dot + 1);
                out += (out.empty() ? "" : ",") + name;
            }
    }
    if (Object* instance = montages ? AnimInstance(context.mesh) : nullptr) {
        Call playing(instance, "IsAnyMontagePlaying");
        if (playing.Slot("ReturnValue", sizeof(bool)) && playing.Invoke() && playing.Get<bool>("ReturnValue") &&
            Call(instance, "Montage_Stop").Set("InBlendOutTime", 0.1f).Set("Montage", static_cast<Object*>(nullptr)).Invoke()) stopped = 1;
    }
    if (stopped) out += out.empty() ? "montage" : "+montage";
    return out.empty() ? "none" : out;
}
// The hero's action ability that started since the chain last looked at the hero's abilities (combo_.active), none of
// the chain's own casts: a press the player made as a boss move ended (the game replays a press held back during the
// move right then). Its name, "" none.
std::string HeroNewAction(const Context& context) {
    Object* asc = PawnAsc(context.pawn);
    Object* type = Class("/Script/GameplayAbilities.GameplayAbilitySpec"), *container = Class("/Script/GameplayAbilities.GameplayAbilitySpecContainer");
    const int32_t specs = ue::Alive(asc) ? Offset(asc, "ActivatableAbilities") : -1, items = container ? ue::Offset(container, "Items") : -1, size = type ? ue::StructSize(type) : 0,
        handle_at = type ? ue::Offset(type, "Handle") : -1, ability_at = type ? ue::Offset(type, "Ability") : -1, count_at = type ? ue::Offset(type, "ActiveCount") : -1;
    if (specs < 0 || items < 0 || size <= 0 || handle_at < 0 || ability_at < 0 || count_at < 0) return {};
    const Array array = ue::At<Array>(asc, specs + items);
    if (array.num <= 0 || array.num > 4096 || array.max < array.num || !FaceReadable(array.data, static_cast<size_t>(array.num) * static_cast<size_t>(size))) return {};
    for (int32_t i = 0; i < array.num; ++i) {
        Object* row = static_cast<Object*>(array.data) + static_cast<size_t>(i) * static_cast<size_t>(size);
        if (!ue::At<uint8_t>(row, count_at)) continue;
        Object* ability = ue::At<Object*>(row, ability_at);
        const int32_t handle = ue::At<int32_t>(row, handle_at);
        if (!AbilityActionBits(ability) || handle == skill_.handle || handle == pending_.handle ||
            std::find(combo_.active.begin(), combo_.active.end(), handle) != combo_.active.end()) continue;
        std::string name = Text(ability); const size_t dot = name.find_last_of("./"); if (dot != std::string::npos) name = name.substr(dot + 1);
        return name;
    }
    return {};
}
void BlinkOut(Object* pawn,bool fx=true){
    if(blink_.on||!ue::Alive(pawn))return;
    blink_={};blink_.on=true;blink_.fx=fx;blink_.pawn=Id(pawn);const ULONGLONG now=GetTickCount64();blink_.show_at=now+kBlinkMaxMs;blink_.until=now+kBlinkMaxMs;
    if(fx)BlinkFx(pawn,kBlinkOutFx,kBlinkOutSound,"out");
    std::vector<Object*> actors{pawn};
    Call attached(pawn,"GetAttachedActors");
    if(attached.Set("bResetArray",true).Set("bRecursivelyIncludeAttachedActors",true).Invoke()){
        const Array list=attached.Get<Array>("OutActors");
        if(list.data&&list.num>0&&list.num<=256)for(int32_t i=0;i<list.num;++i)actors.push_back(static_cast<Object**>(list.data)[i]);
        ue::Free(list.data);}
    for(Object* actor:actors){bool hidden=false;if(!ue::Alive(actor)||!Bool(actor,"bHidden",hidden)||hidden)continue;
        if(Call(actor,"SetActorHiddenInGame").Set("bNewHidden",true).Invoke())blink_.hidden.push_back({Id(actor)});}
}
// The change is done: shown again kBlinkHoldMs later (DriveBlink).
void BlinkDone(){if(blink_.on)blink_.show_at=GetTickCount64()+kBlinkHoldMs;}
void BlinkShowNow(){if(blink_.on)blink_.show_at=GetTickCount64();}
void DriveBlink(){
    if(!blink_.on)return;const ULONGLONG now=GetTickCount64();if(now<blink_.show_at)return;
    size_t shown=0;for(const BlinkHidden& h:blink_.hidden)if(Same(h.actor)&&Call(h.actor.object,"SetActorHiddenInGame").Set("bNewHidden",false).Invoke())++shown;
    if(blink_.fx&&Same(blink_.pawn))BlinkFx(blink_.pawn.object,kBlinkInFx,kBlinkInSound,"in");
    Logf("NPC_BLINK shown actors=%zu/%zu after=%s",shown,blink_.hidden.size(),now>=blink_.until?"timeout":"change");
    blink_={};
}
// ---- 投技分身 (GrabBody) every frame -------------------------------------------------------------------------------
void EndGrabBody(GrabBody& g,const char* why,bool completed){
    if(Same(g.actor)){if(completed)BlinkFx(g.actor.object,kBlinkOutFx,kBlinkOutSound,"grab-body out",g.size);Call(g.actor.object,"K2_DestroyActor").Invoke();}
    const ULONGLONG now=GetTickCount64();
    if(g.token)MotianGrab::RequestRelease(g.token);  // the target is let go
    if(completed&&g.damage>=0&&g.token)grab_owed_.push_back({g.damage,g.token,now});  // dealt once its side has ended
    else GrabExtras::HandOffEnd(g.damage,completed);
    Logf("NPC_COMBO grab body %s after %llums at %.2f/%.2f s (%s)%s",completed?"done":"removed",now-g.at,g.pos,g.length,why,g.token?"; its target is let go":"");
    g.damage=-1;g.actor={};g.token=0;
}
// The target goes on with its grabbed animation: played again where it would be now if the cancel stopped it (only
// right after the cut, so it never takes over what the target does later).
void DriveGrabVictim(GrabBody& g,ULONGLONG now){
    if(g.victim_done||!g.victim_montage.object)return;
    if(g.token&&MotianGrab::GetSnapshot().holding_token==g.token)return;  // the target's own grabbed ability plays it
    if(!Same(g.victim)||!Same(g.victim_mesh)||!Same(g.victim_montage)){g.victim_done=true;return;}
    const double at=g.victim_start+(g.pos-g.start);
    if(at>=g.victim_length-0.05||now>g.at+kGrabVictimMs){g.victim_done=true;return;}
    Object* anim=AnimInstance(g.victim_mesh.object);if(!anim||BodyMontage(anim)==g.victim_montage.object)return;  // still on it
    Call play(anim,"Montage_Play");
    const bool played=play.Set("MontageToPlay",g.victim_montage.object).Set("InPlayRate",1.f).Set("ReturnValueType",uint8_t{0})
        .Set("InTimeToStartMontageAt",static_cast<float>(at)).Set("bStopAllMontages",true).Invoke();
    g.victim_done=true;
    Logf("NPC_COMBO grab body: target montage %s played again at %.2f s (the cancel stopped it) ok=%d",Text(g.victim_montage.object).c_str(),at,played?1:0);
}
// The damage of bodies that ended, once their target has been let go.
void DriveGrabOwed(ULONGLONG now){
    if(grab_owed_.empty())return;
    const MotianGrab::Snapshot s=MotianGrab::GetSnapshot();
    for(GrabOwed& o:grab_owed_){
        const bool let_go=s.holding_token!=o.token&&!s.active&&!s.cleanup_pending;
        if(!let_go&&now<o.at+kGrabReleaseWaitMs)continue;
        Logf("NPC_COMBO grab body damage after the target was let go (%llums, %s)",now-o.at,let_go?"its side ended":"wait over");
        GrabExtras::HandOffEnd(o.damage,true);o.damage=-1;
    }
    grab_owed_.erase(std::remove_if(grab_owed_.begin(),grab_owed_.end(),[](const GrabOwed& o){return o.damage<0;}),grab_owed_.end());
}
void DriveGrabBodies(){
    const ULONGLONG now=GetTickCount64();
    DriveGrabOwed(now);
    if(grab_bodies_.empty())return;
    Object* statics=ue::Default("/Script/Engine.Default__GameplayStatics");
    for(GrabBody& g:grab_bodies_){
        if(!g.actor.object)continue;
        if(!Same(g.actor)||!Same(g.body)||!Same(g.montage)){EndGrabBody(g,"the body or its montage is gone",false);continue;}
        Object* body=g.body.object;++g.frames;
        if(!g.placed){
            // Its pose at the cut is evaluated once before it is shown: the root, where the pose has it (root motion stays
            // in the pose), goes where the look's root was.
            if(g.frames<2&&now<g.at+500)continue;
            Call local(body,"GetSocketTransform");Transform inverse,world;bool moved=false;
            if(local.Set("InSocketName",g.root_bone).Set("TransformSpace",uint8_t{2}).Invoke()){
                Call invert(Math(),"InvertTransform");
                if(invert.Set("T",local.Get<Transform>("ReturnValue")).Invoke()&&Compose(invert.Get<Transform>("ReturnValue"),g.root_world,world)&&Same(g.root))
                    moved=Call(g.root.object,"K2_SetWorldTransform").Set("NewTransform",world).Set("bSweep",false).Set("bTeleport",true).Invoke();
            }
            Call(g.actor.object,"SetActorHiddenInGame").Set("bNewHidden",false).Invoke();
            g.placed=true;g.tick=now;Logf("NPC_COMBO grab body shown after %d frames (root placed=%d)",g.frames,moved?1:0);
        }
        if(!g.ended){
            // The world's time step (time dilation and hit stops included); the wall clock when it is unreadable.
            Call delta(statics,"GetWorldDeltaSeconds");
            double dt=statics&&delta.Set("WorldContextObject",g.actor.object).Invoke()?delta.Get<double>("ReturnValue"):-1.0;
            if(!(dt>0&&dt<0.5))dt=(std::min)(static_cast<double>(now-(g.tick?g.tick:now))/1000.0,0.1);
            g.tick=now;
            if(dt>0)g.pos=(std::min)(g.pos+dt,g.length);
            Call(body,"SetPosition").Set("InPos",static_cast<float>(g.pos)).Set("bFireNotifies",false).Invoke();
            DriveGrabVictim(g,now);
            if(g.pos>=g.length-0.02)g.ended=now;
        }
        if(g.ended&&now>=g.ended+kGrabBodyHoldMs)EndGrabBody(g,"its grab played out",true);
        else if(now>g.deadline)EndGrabBody(g,"time limit",true);
    }
    grab_bodies_.erase(std::remove_if(grab_bodies_.begin(),grab_bodies_.end(),[](const GrabBody& g){return !g.actor.object;}),grab_bodies_.end());
}
// ---- 瞬移 ----
// The hero's own 瞬身 dodge (its SpecialDash ability: montage, effects and sounds as they are), landing in front of
// the target: the locked-on enemy, else the nearest hostile within 60 m. The landing is `dist` cm from the target
// along its facing (never closer than the two capsules allow), `lift` cm above the ground there, facing the target;
// a wall in between, no ground or no room there tries shorter distances (NpcCombo::TeleportDistances). The hero is
// moved while hidden: by the game's own hide (the hero look: its mesh or actor hidden), else by a quiet hide of ours
// (no extra effect) 300 ms into the dodge (120 ms in a look), and kept there while hidden so the dodge's root motion does not carry it
// on. No target or no room: a plain 瞬身. No 瞬身 on this weapon, or the game refuses it now: the blink alone.
struct TraceColor { float r=0,g=0,b=0,a=0; };
// The game's own hide is waited for longer in the hero look (where it is seen); a look's display mesh is ours.
constexpr ULONGLONG kTeleportHideWaitMs=120,kTeleportNativeWaitMs=300,kTeleportQuietHoldMs=220,kTeleportHoldMaxMs=1500;constexpr double kTeleportRangeCm=6000;
Object* KismetSystem(){return ue::Default("/Script/Engine.Default__KismetSystemLibrary");}
bool ActorAt(Object* actor,Vector& at){
    Call where(actor,"K2_GetActorLocation");if(!where.Slot("ReturnValue",sizeof(Vector))||!where.Invoke())return false;
    at=where.Get<Vector>("ReturnValue");return std::isfinite(at.x+at.y+at.z);
}
bool CapsuleOf(Object* actor,double& half,double& radius){
    Object* capsule=Property<Object*>(actor,"CapsuleComponent");if(!ue::Alive(capsule))return false;
    Call h(capsule,"GetScaledCapsuleHalfHeight"),r(capsule,"GetScaledCapsuleRadius");
    if(!h.Invoke()||!r.Invoke())return false;
    half=h.Get<float>("ReturnValue");radius=r.Get<float>("ReturnValue");
    return std::isfinite(half+radius)&&radius>=1&&radius<=300&&half>=radius&&half<=600;
}
// The actors the landing traces pass through: the hero, the target and what is attached to either (weapons).
std::vector<Object*> TraceIgnore(Object* hero,Object* target){
    std::vector<Object*> out;
    for(Object* actor:{hero,target}){if(!ue::Alive(actor))continue;out.push_back(actor);
        Call attached(actor,"GetAttachedActors");
        if(attached.Set("bResetArray",true).Set("bRecursivelyIncludeAttachedActors",true).Invoke()){
            const Array list=attached.Get<Array>("OutActors");
            if(list.data&&list.num>0&&list.num<=256)for(int32_t i=0;i<list.num;++i){Object* a=static_cast<Object**>(list.data)[i];if(ue::Alive(a))out.push_back(a);}
            ue::Free(list.data);}}
    return out;
}
// A line trace on the visibility channel, `skip` ignored: 1 hit (point / normal / actor filled), 0 clear, -1 unknown.
int TraceLine(Object* context,const Vector& from,const Vector& to,std::vector<Object*>& skip,Vector* point=nullptr,Vector* normal=nullptr,Object** actor=nullptr){
    const Array ignore{skip.data(),static_cast<int32_t>(skip.size()),static_cast<int32_t>(skip.size())};
    Call trace(KismetSystem(),"LineTraceSingle");
    if(!trace.Set("WorldContextObject",context).Set("Start",from).Set("End",to).Set("TraceChannel",uint8_t{0}).Set("bTraceComplex",false)
        .Set("ActorsToIgnore",ignore).Set("DrawDebugType",uint8_t{0}).Set("bIgnoreSelf",true).Set("TraceColor",TraceColor{}).Set("TraceHitColor",TraceColor{})
        .Set("DrawTime",0.f).Invoke())return -1;
    if(!trace.Get<bool>("ReturnValue"))return 0;
    if(point||normal||actor){
        // The hit copied at the engine's own HitResult size (BreakHitResult belongs to GameplayStatics in this build).
        const int32_t size=ue::StructSize(Class("/Script/Engine.HitResult"));
        Call split(ue::Default("/Script/Engine.Default__GameplayStatics"),"BreakHitResult");
        if(!split.Ok())split=Call(KismetSystem(),"BreakHitResult");
        unsigned char* from_hit=size>0&&size<512?trace.Slot("OutHit",static_cast<size_t>(size)):nullptr;
        unsigned char* to_hit=from_hit?split.Slot("Hit",static_cast<size_t>(size)):nullptr;
        if(!to_hit||!split.Slot("ImpactPoint",sizeof(Vector))||!split.Slot("ImpactNormal",sizeof(Vector)))return -1;
        const bool with_actor=actor&&split.Slot("HitActor",sizeof(Object*));
        std::memcpy(to_hit,from_hit,static_cast<size_t>(size));
        if(!split.Invoke())return -1;
        if(point)*point=split.Get<Vector>("ImpactPoint");
        if(normal)*normal=split.Get<Vector>("ImpactNormal");
        if(actor){Object* hit=with_actor?split.Get<Object*>("HitActor"):nullptr;*actor=ue::Alive(hit)?hit:nullptr;}
    }
    return 1;
}
// Room for a capsule at `at` (`skip` ignored): 1 blocked, 0 clear, -1 unknown.
int TraceCapsule(Object* context,const Vector& at,double half,double radius,std::vector<Object*>& skip){
    const Array ignore{skip.data(),static_cast<int32_t>(skip.size()),static_cast<int32_t>(skip.size())};
    Call trace(KismetSystem(),"CapsuleTraceSingle");
    if(!trace.Set("WorldContextObject",context).Set("Start",at).Set("End",at).Set("Radius",static_cast<float>(radius)).Set("HalfHeight",static_cast<float>(half))
        .Set("TraceChannel",uint8_t{0}).Set("bTraceComplex",false).Set("ActorsToIgnore",ignore).Set("DrawDebugType",uint8_t{0})
        .Set("bIgnoreSelf",true).Set("TraceColor",TraceColor{}).Set("TraceHitColor",TraceColor{}).Set("DrawTime",0.f).Invoke())return -1;
    return trace.Get<bool>("ReturnValue")?1:0;
}
bool EnemyOf(Object* pawn,Object* hero){DamageTarget probe;return ue::Alive(pawn)&&pawn!=hero&&ue::IsA(pawn,Class("/Script/Engine.Pawn"))&&DamageCandidate(pawn,hero,probe);}
// The enemy a reflected block names, the pawn itself or one of its components: a weak pointer (object index, then
// its serial) at any 4-byte step, or a pointer at any 8-byte step. The hero is never it.
Object* EnemyIn(unsigned char* bytes,int32_t size,Object* hero,int32_t& at){
    if(!bytes||size<4||size>1024)return nullptr;
    Object* component=Class("/Script/Engine.ActorComponent");
    const auto enemy=[&](Object* o)->Object*{
        if(!ue::Alive(o)||o==hero)return nullptr;
        if(EnemyOf(o,hero))return o;
        if(component&&ue::IsA(o,component)){Call owner(o,"GetOwner");Object* a=owner.Invoke()?owner.Get<Object*>("ReturnValue"):nullptr;if(a!=hero&&EnemyOf(a,hero))return a;}
        return nullptr;};
    const int32_t count=ue::detail::ObjectCount();
    for(int32_t off=0;off+4<=size;off+=4){
        const int32_t index=ue::At<int32_t>(bytes,off);
        if(index>0&&index<count&&off+8<=size){Object* o=ue::detail::ObjectAt(index);const Identity id=Id(o);
            if(id.serial&&id.serial==ue::At<int32_t>(bytes,off+4))if(Object* e=enemy(o)){at=off;return e;}}
        if(off%8==0&&off+8<=size)if(Object* e=enemy(ue::At<Object*>(bytes,off))){at=off;return e;}
    }
    return nullptr;
}
int32_t ParamSize(Object* function,const char* name){
    ue::Field fields[32];const size_t n=ue::Fields(function,fields,std::size(fields));
    for(size_t i=0;i<n;++i)if(std::strcmp(fields[i].name,name)==0)return fields[i].size;
    return -1;
}
// The enemy the hero is locked on, or null; `how` says where it was read ("none": no lock; "unread": a lock that
// none of the reads below could name). The soft-lock handler's answer with the lock kept (its whole Target struct,
// at the size the engine gives it), else the component's lock getters, else its own properties naming a lock or
// target. The first lock that cannot be read logs those blocks once.
Object* LockedEnemy(Object* hero,const char*& how){
    how="none";
    Object* targeting=TargetingComponent(hero);if(!targeting||!HasLock(targeting))return nullptr;
    how="unread";
    Object* cls=ue::ClassOf(targeting);int32_t at=-1;static bool logged=false;std::string seen;
    static const Name soft=ue::MakeName(L"Targeting.Handler.CombatSoftLock");
    if(!soft.None()){
        const int32_t size=ParamSize(ue::FindFunction(cls,"FindTargetByHandler"),"Target");
        Call find(targeting,"FindTargetByHandler");
        if(size>0&&find.Set("HandlerTag",soft).Set("UseLockTarget",true).Invoke()&&find.Get<bool>("ReturnValue"))
            if(unsigned char* p=find.Slot("Target",static_cast<size_t>(size))){
                if(Object* e=EnemyIn(p,size,hero,at)){how="lock";return e;}
                if(!logged){char hex[3*64+1]{};for(int32_t i=0;i<size&&i<64;++i)std::snprintf(hex+3*i,4,"%02x ",p[i]);seen+=" FindTargetByHandler.Target["+std::to_string(size)+"]="+hex;}}
    }
    for(const char* name:{"GetLockTarget","GetLockTargetActor","GetLockedTarget","GetLockedActor","GetCurrentLockTarget","GetLockOnTarget","GetLockTargetData","GetCurrentTarget","GetTargetActor"}){
        Object* fn=ue::FindFunction(cls,name);if(!fn)continue;
        ue::Field fields[8];const size_t n=ue::Fields(fn,fields,std::size(fields));
        if(n!=1||std::strcmp(fields[0].name,"ReturnValue")!=0||fields[0].size<=0)continue;  // getters with no inputs only
        Call get(targeting,name);if(!get.Invoke())continue;
        if(unsigned char* p=get.Slot("ReturnValue",static_cast<size_t>(fields[0].size)))if(Object* e=EnemyIn(p,fields[0].size,hero,at)){how=name;return e;}
        if(!logged)seen+=std::string(" ")+name+"()";
    }
    for(Object* s:{cls,Class("/Script/SGameCombatSystem.TargetingComponent")}){
        if(!s)continue;ue::Field fields[256];const size_t n=ue::Fields(s,fields,std::size(fields));
        for(size_t i=0;i<n;++i){const std::string lower=Lower(fields[i].name);
            if(lower.find("lock")==std::string::npos&&lower.find("target")==std::string::npos)continue;
            if(fields[i].offset<0||fields[i].size<=0||fields[i].size>1024)continue;
            if(Object* e=EnemyIn(static_cast<unsigned char*>(static_cast<void*>(targeting))+fields[i].offset,fields[i].size,hero,at)){
                static std::string name;name=std::string("property ")+fields[i].name;how=name.c_str();return e;}
            if(!logged)seen+=std::string(" .")+fields[i].name+"@"+std::to_string(fields[i].offset)+"["+std::to_string(fields[i].size)+"]";}
    }
    if(!logged){logged=true;Logf("NPC_COMBO lock held but unread on %s:%s",Text(cls).c_str(),seen.c_str());}
    return nullptr;
}
Object* NearestHostile(Object* hero,double range){
    Vector self{};if(!ActorAt(hero,self))return nullptr;
    Call actors(ue::Default("/Script/Engine.Default__GameplayStatics"),"GetAllActorsOfClass");
    if(!actors.Set("WorldContextObject",hero).Set("ActorClass",Class("/Script/Engine.Pawn")).Invoke())return nullptr;
    const Array list=actors.Get<Array>("OutActors");double best=range*range;Object* found=nullptr;
    if(list.data&&list.num>0&&list.num<=2048&&FaceReadable(list.data,static_cast<size_t>(list.num)*sizeof(Object*)))
        for(int32_t i=0;i<list.num;++i){Object* pawn=static_cast<Object**>(list.data)[i];Vector p{};
            if(!ue::Alive(pawn)||pawn==hero||!ActorAt(pawn,p))continue;
            const double d=(p.x-self.x)*(p.x-self.x)+(p.y-self.y)*(p.y-self.y)+(p.z-self.z)*(p.z-self.z);
            if(d<best&&EnemyOf(pawn,hero)){best=d;found=pawn;}}
    ue::Free(list.data);return found;
}
// SetLockTarget on one enemy (钩子 without a lock: the nearest). The game's Target struct (read 11-08, npcretarget
// lockstruct): {USceneComponent* (the enemy's mesh), FName socket (spine_04)}, 16 bytes. The soft pick goes on as it is
// when it is that enemy; otherwise the struct is built from the enemy's own mesh and the pick's socket when that mesh
// has it (else spine_04 / spine_03 / pelvis, else none). Byte 0 is only ever a live component: the 11-08 build put an
// object index there, the game read it as a pointer and the hook stopped on an access violation. Returns how
// ("soft-pick", "built", "built (no socket)"), or why not.
const char* LockOn(Object* pawn,Object* target){
    Object* targeting=TargetingComponent(pawn);if(!targeting)return "no-targeting-component";
    if(!ue::Alive(target))return "no-target";
    static const Name soft=ue::MakeName(L"Targeting.Handler.CombatSoftLock");
    LockTarget t;Object* picked=nullptr;int32_t at=-1;bool have=false;
    if(!soft.None()){Call find(targeting,"FindTargetByHandler");
        if(find.Set("HandlerTag",soft).Set("UseLockTarget",false).Invoke()&&find.Get<bool>("ReturnValue")){
            t=find.Get<LockTarget>("Target");have=true;picked=EnemyIn(t.bytes,static_cast<int32_t>(sizeof(t.bytes)),pawn,at);}}
    const char* how="soft-pick";
    if(picked!=target){
        Object* mesh=Property<Object*>(target,"Mesh");
        if(!ue::Alive(mesh))return "target-has-no-mesh";
        const auto has=[&](const Name& n){if(n.None())return false;Call exists(mesh,"DoesSocketExist");return exists.Set("InSocketName",n).Invoke()&&exists.Get<bool>("ReturnValue");};
        Name socket{};const Name pick_socket=have?Name{ue::At<uint32_t>(t.bytes,8),ue::At<uint32_t>(t.bytes,12)}:Name{};
        if(has(pick_socket))socket=pick_socket;
        else for(const wchar_t* n:{L"spine_04",L"spine_03",L"pelvis"}){const Name name=ue::MakeName(n);if(has(name)){socket=name;break;}}
        t={};ue::At<Object*>(t.bytes,0)=mesh;ue::At<uint32_t>(t.bytes,8)=socket.index;ue::At<uint32_t>(t.bytes,12)=socket.number;
        how=socket.None()?"built (no socket)":"built";
    }
    Call set(targeting,"SetLockTarget");
    if(!set.Set("Target",t).Invoke())return "set-call-failed";
    if(!set.Get<bool>("ReturnValue"))return "set-refused";
    const char* via="none";
    return LockedEnemy(pawn,via)==target?how:"set-but-reads-other";
}
// 视角跟随: while a move of the chain plays (a skill cast by the NPC key, any montage of a boss look too), the view is a
// camera of our own (a CameraActor made once, set as the player's view target, blended in over kCamInS and back to the
// game's camera over kCamOutS), placed every frame from the shown model rather than from the actor the game's camera
// hangs on. A boss move's body leaves the capsule (MoTian Skill_06 rises 7 m in half a second), and the game camera
// trails its pivot by ~0.2 s, falls behind the actor (Skill_09: 7.8 m below it) and folds under roofs. 11-12 attempts on
// the game camera: moving its pivot (a target offset) left the body 78 deg off centre in 56 of 122 frames; turning the
// view pitched it up to 81 deg (a view from below); correcting its eye from outside chased every attack's sway and kept
// it 1-3 m off its place. Our camera gives the hero's own view (user 11-12 "和主角差不多的视角"):
//  - it looks at a target: the bone 镜头跟随 picks (ViewBodyPoint: pelvis, neck or head) through a dead zone
//    (kCamDeadFlat across, kCamDeadRise up / down: an attack's own sway leaves the camera still), eased over
//    kCamFollowS; at once after a pose jump (a trim seek);
//  - it sits at the target plus the game camera's own relation to that bone (ViewNative: measured all the time the
//    game's camera is the view, between moves, in its rendered axes, eased over kNativeS), turned by the player's control
//    rotation (the mouse and the lock-on keep turning the view); a model taller than kHeroCm pushes it out in proportion;
//    never below the target (kCamMinDown). 11-12 round 1 kept that relation per mesh and lost it whenever the shown mesh
//    changed: the fallback put the eye 141 cm over the pelvis, the pelvis 17-37 deg under the view centre;
//  - it comes in front of a wall between the target and it at once, and goes back out over kCamWallOutS.
// Its rotation is the control rotation, its field of view the game camera's when it took over. When the game sets
// another view target meanwhile (a cinematic) the camera lets go and takes over again only from the game's camera.
// A gap of up to kCamGraceMs between two moves keeps it. One NPC_VIEW line per move (plus a trace when off centre).
double ClockSeconds(){LARGE_INTEGER hz{},t{};QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&t);return static_cast<double>(t.QuadPart)/static_cast<double>(hz.QuadPart);}
double Gap3(const Vector& a,const Vector& b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}
struct ViewFollow { Identity pawn,mesh;int node=-1;int64_t handle=0;bool on=false;Vector base{};ULONGLONG started=0,trace_at=0;
                    uint64_t visit=0,cast_start=0;double max_flat=0,max_rise=0,max_angle=0,size=0,dist=0,min_wall=0;
                    int frames=0,off_frames=0,wall_frames=0,seeks=0,trace_n=0,passed=0;std::string move,blocker,trace; };
constexpr ULONGLONG kViewSnapMs=150,kViewSkipRefreshMs=250;
// Not a wall for the camera: an actor of the chain's (its bullets and effects, attached or not), another pawn, water.
bool ViewPassThrough(Object* actor,Object* pawn){
    if(!ue::Alive(actor))return false;
    if(actor==pawn||ue::IsA(actor,Class("/Script/Engine.Pawn")))return true;
    if(Text(ue::ClassOf(actor)).find("WaterBody")!=std::string::npos)return true;  // a lake's surface (MoTian Skill_09 held short by one)
    for(const char* field:{"Owner","Instigator","BulletOwnerAvatar"})if(Property<Object*>(actor,field)==pawn)return true;
    Call parent(actor,"GetAttachParentActor");return parent.Invoke()&&parent.Get<Object*>("ReturnValue")==pawn;
}
ViewFollow view_;bool view_enabled_=true;int view_space_=0;
// 0: offsets in world axes, 1: in the control yaw's frame (npcretarget viewfollow world|local)
Object* HeroCamera(Object* pawn){
    Object* atpc=Class("/Script/AdvancedThirdPersonCamera.ATPCCameraComponent");if(!atpc||!ue::Alive(pawn))return nullptr;
    Call get(pawn,"GetComponentByClass");Object* camera=get.Set("ComponentClass",atpc).Invoke()?get.Get<Object*>("ReturnValue"):nullptr;
    return ue::Alive(camera)?camera:nullptr;
}
Object* PlayerController(Object* context_object){
    Call get(ue::Default("/Script/Engine.Default__GameplayStatics"),"GetPlayerController");
    Object* pc=get.Set("WorldContextObject",context_object).Set("PlayerIndex",int32_t{0}).Invoke()?get.Get<Object*>("ReturnValue"):nullptr;return ue::Alive(pc)?pc:nullptr;
}
// The offset in the space the camera takes it (view_space_).
bool SendViewOffset(Object* camera,Object* pawn,const Vector& world,float blend){
    static const Name name=ue::MakeName(L"PBZ_ViewFollow");if(name.None()||!ue::Alive(camera))return false;
    Vector offset=world;
    if(view_space_==1){Object* pc=PlayerController(pawn);RotatorD r{};if(pc){Call get(pc,"GetControlRotation");if(get.Invoke())r=get.Get<RotatorD>("ReturnValue");}
        const double y=-r.yaw*3.14159265358979323846/180.0;offset={world.x*std::cos(y)-world.y*std::sin(y),world.x*std::sin(y)+world.y*std::cos(y),world.z};}
    return Call(camera,"SetAdditiveParameter_TargetOffset").Set("AdditiveName",name).Set("TargetOffset",offset).Set("BlendTime",blend).Invoke();
}
double ActorYaw(Object* actor){Call r(actor,"K2_GetActorRotation");return r.Invoke()?r.Get<RotatorD>("ReturnValue").yaw:0;}
constexpr float kCamInS=0.2f,kCamOutS=0.35f;
constexpr double kCamDeadFlat=25,kCamDeadRise=35,kCamFollowS=0.05,kCamMinDown=0,kCamWallOutS=0.25,kCamWallGapCm=25,kCamNearCm=60;
constexpr double kHeroCm=200,kNativeS=0.3;
constexpr ULONGLONG kCamGraceMs=200;
// The game camera's relation to the bone when it was never measured: the hero's (11-12 logs, the pelvis: (-341,81,40),
// (-358,83,47); the head: (-345,83,-25)), in the view's axes (forward, right, up).
Vector CamDefault(int target){return Vector{-350,82,target==NpcCombo::kViewPelvis?45.0:target==NpcCombo::kViewNeck?-15.0:-25.0};}
// The game camera's relation to the bone 镜头跟随 picks, kept fresh while it is the view (between moves): the eye from
// the bone in the game camera's rendered axes, eased over kNativeS (its own lag and an idle's sway averaged out).
struct ViewNative { Identity pawn;int target=-1;Vector rel{};bool known=false; };
ViewNative view_native_;
struct ViewCam { Identity actor,component,pawn;bool on=false,placed=false;Vector target{};double wall=-1,clock=0;
                 ULONGLONG wanted_at=0,released_at=0,snap_until=0,skip_at=0,blend_until=0;std::vector<Object*> skip; };
ViewCam view_cam_;
Object* ViewTargetOf(Object* pc){Call get(pc,"GetViewTarget");Object* t=get.Invoke()?get.Get<Object*>("ReturnValue"):nullptr;return ue::Alive(t)?t:nullptr;}
bool BlendViewTo(Object* pc,Object* target,float seconds){  // EViewTargetBlendFunction::VTBlend_Cubic = 1
    return Call(pc,"SetViewTargetWithBlend").Set("NewViewTarget",target).Set("BlendTime",seconds).Set("BlendFunc",uint8_t{1}).Set("BlendExp",2.0f).Set("bLockOutgoing",false).Invoke();
}
// Our CameraActor (made once, owned by the hero; again after a level change).
Object* ViewCamActor(Object* pawn){
    if(Same(view_cam_.actor)&&Same(view_cam_.component))return view_cam_.actor.object;
    view_cam_.actor={};view_cam_.component={};
    Object* statics=ue::Default("/Script/Engine.Default__GameplayStatics"),*cls=Class("/Script/Engine.CameraActor");if(!statics||!cls)return nullptr;
    Transform at;{Call get(pawn,"GetTransform");if(get.Invoke())at=get.Get<Transform>("ReturnValue");}
    Call begin(statics,"BeginDeferredActorSpawnFromClass");
    if(!begin.Set("WorldContextObject",pawn).Set("ActorClass",cls).Set("SpawnTransform",at).Set("CollisionHandlingOverride",uint8_t{1}).Set("Owner",pawn).Invoke())return nullptr;
    Object* actor=begin.Get<Object*>("ReturnValue");if(!ue::Alive(actor))return nullptr;
    if(!Call(statics,"FinishSpawningActor").Set("Actor",actor).Set("SpawnTransform",at).Invoke()){Call(actor,"K2_DestroyActor").Invoke();Logf("NPC_VIEW camera: FinishSpawningActor refused");return nullptr;}
    Object* component=Property<Object*>(actor,"CameraComponent");
    if(!ue::Alive(component)){Call(actor,"K2_DestroyActor").Invoke();Logf("NPC_VIEW camera: CameraActor without a CameraComponent");return nullptr;}
    Call(component,"SetConstraintAspectRatio").Set("bInConstrainAspectRatio",false).Invoke();
    view_cam_.actor=Id(actor);view_cam_.component=Id(component);Logf("NPC_VIEW camera %s made",Text(actor).c_str());
    return actor;
}
// Back to the game's camera when ours is the view (blended); `why` logged.
void ReleaseViewCam(const char* why){
    if(!view_cam_.on)return;
    view_cam_.on=false;view_cam_.placed=false;view_cam_.released_at=GetTickCount64();
    Object* pc=Same(view_cam_.pawn)?PlayerController(view_cam_.pawn.object):nullptr;
    const bool ours=pc&&Same(view_cam_.actor)&&ViewTargetOf(pc)==view_cam_.actor.object;
    const bool back=ours&&BlendViewTo(pc,view_cam_.pawn.object,kCamOutS);
    Logf("NPC_VIEW camera released (%s): %s",why,back?"blending back to the game's camera":ours?"the blend was refused":"the view was not ours any more");
}
void EndViewFollow(const char* why,bool){
    if(!view_.on)return;
    Logf("NPC_VIEW %s: body up to %.0fcm across and %.0fcm up/down from where it started; model %.0fcm tall framed from %.0fcm; up to %.0f deg off the view centre "
         "(over 30 deg %d of %d frames); in front of a wall %d frames (down to %.0fcm%s%s, %d hits on the chain's own actors, pawns or water passed); %llums; %s; seeks=%d",
         view_.move.c_str(),view_.max_flat,view_.max_rise,view_.size,view_.dist,view_.max_angle,view_.off_frames,view_.frames,view_.wall_frames,view_.min_wall,
         view_.blocker.empty()?"":", last ",view_.blocker.c_str(),view_.passed,GetTickCount64()-view_.started,why,view_.seeks);
    if(view_.off_frames>=5&&!view_.trace.empty())Logf("NPC_VIEW trace %s (t s: body rise cm, eye over target cm, pitch/yaw off deg):%s",view_.move.c_str(),view_.trace.c_str());
    view_={};
}
// The point 视角跟随 looks at on any skeleton: the bone 镜头跟随 picks (NpcCombo::ViewTarget: 骨盆 by default, 脖子 or
// 头部; user 11-12). By name first (the names the game's and the bosses' rigs use), the other of neck / head next for
// those two, then the first bone among the first 160 whose name has it in it, then the pelvis; a creature rig with none
// of them: the mesh's bounds, its centre for the pelvis, 80 % up for the neck and head. Kept per mesh and choice (`how`
// for the log).
struct ViewBody { Identity mesh;int target=-1;Name bone{};bool bounds=false;std::string how; };
ViewBody view_body_;
bool ViewBodyPoint(Object* mesh,Vector& out){
    const int target=NpcCombo::ViewTarget();
    if(!Same(view_body_.mesh)||view_body_.mesh.object!=mesh||view_body_.target!=target){
        view_body_={};view_body_.mesh=Id(mesh);view_body_.target=target;
        const auto exists=[&](const Name& n){if(n.None())return false;Call index(mesh,"GetBoneIndex");return index.Set("BoneName",n).Invoke()&&index.Get<int32_t>("ReturnValue")>=0;};
        const auto by_name=[&](std::initializer_list<const wchar_t*> names,const char* how){
            if(!view_body_.bone.None())return true;
            for(const wchar_t* name:names){const Name n=ue::MakeName(name);if(exists(n)){view_body_.bone=n;view_body_.how=how;return true;}}
            return false;};
        const auto by_part=[&](const char* part){
            if(!view_body_.bone.None())return true;
            for(int32_t i=0;i<160;++i){
                Call get(mesh,"GetBoneName");if(!get.Set("BoneIndex",i).Invoke())break;
                const Name n=get.Get<Name>("ReturnValue");if(n.None())break;
                char text[96]{};ue::NameText(n,text,sizeof(text));for(char& ch:text)ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if(std::strstr(text,part)){view_body_.bone=n;view_body_.how=std::string("bone ")+text;return true;}
            }
            return false;};
        const auto head=[&]{return by_name({L"head",L"Head",L"Bip001-Head",L"Bip001 Head",L"Bip01-Head",L"Bip01 Head",L"head_01"},"head by name");};
        const auto neck=[&]{return by_name({L"neck_01",L"neck",L"Neck",L"Bip001-Neck",L"Bip001 Neck",L"Bip01-Neck",L"Bip01 Neck",L"neck_02"},"neck by name");};
        const auto pelvis=[&]{return by_name({L"pelvis",L"Pelvis",L"Bip001-Pelvis",L"Bip001 Pelvis",L"Bip01-Pelvis",L"Hips",L"hips",L"Hip",L"hip",L"root_pelvis"},"pelvis by name");};
        if(target==NpcCombo::kViewHead){head();neck();by_part("head");by_part("neck");pelvis();}
        else if(target==NpcCombo::kViewNeck){neck();head();by_part("neck");by_part("head");pelvis();}
        else{pelvis();by_part("pelvis");by_part("hip");}
        if(view_body_.bone.None()){view_body_.bounds=true;view_body_.how=target==NpcCombo::kViewPelvis?"bounds centre":"80% up the bounds";}
    }
    if(!view_body_.bounds){Call at(mesh,"GetSocketLocation");if(!at.Set("InSocketName",view_body_.bone).Invoke())return false;out=at.Get<Vector>("ReturnValue");}
    else{Call b(KismetSystem(),"GetComponentBounds");if(!b.Set("Component",mesh).Invoke())return false;
        out=b.Get<Vector>("Origin");if(view_body_.target!=NpcCombo::kViewPelvis)out.z+=b.Get<Vector>("BoxExtent").z*0.6;}
    return std::isfinite(out.x+out.y+out.z);
}
// 取景: the shown model's height (its bounds when first shown, standing, without the BoundsScale a display with no
// physics asset gets: ViewFollowState::ModelHeight), kept per mesh at its own size and framed times the scale it is
// shown at now: 主角体型 shrinks the look display (SetRelativeScale3D), and 彭猛 at the hero's size was framed for 490 cm.
struct ViewModel { Identity mesh;double own=0; };
ViewModel view_model_;
double ShownScale(Object* mesh){const Vector s=Property<Vector>(mesh,"RelativeScale3D");return std::isfinite(s.z)&&s.z>0.01?s.z:1.0;}
double ViewModelSize(Object* mesh){
    const double shown=ShownScale(mesh);
    if(Same(view_model_.mesh)&&view_model_.mesh.object==mesh)return view_model_.own*shown;
    view_model_={};view_model_.mesh=Id(mesh);
    Call b(KismetSystem(),"GetComponentBounds");
    const double extent=b.Set("Component",mesh).Invoke()?b.Get<Vector>("BoxExtent").z:0;
    const double scale=Property<float>(mesh,"BoundsScale");
    view_model_.own=ViewFollowState::ModelHeight(extent,scale,shown);
    Logf("NPC_VIEW model height of %s: bounds %.0fcm / boundsScale %.2f / shown x%.3f -> %.0fcm at its own size",Text(mesh).c_str(),extent*2,scale,shown,view_model_.own);
    return view_model_.own*shown;
}
// 主角体型: while a move wired to it plays (NpcCombo::HeroSizeFor), the boss look's shown model is scaled to the hero's
// height (NpcHeroSize::Target of the look's ratio), eased over NpcHeroSize::kEaseMs. The display hangs on the hero mesh
// at the feet, so it shrinks onto the ground; its parts, the boss weapons and the colliders on it scale with it, and the
// grip adapter takes hero_size_shown_ for the hero weapons in its hands. The game camera's lift (FitCameraLift), the
// 视角跟随 framing (ViewModelSize) and the blink bursts (BlinkSize) follow the size shown. The actor, its capsule and the
// move's travel stay the boss's. A look change or a restore sets the display back to its own size at once.
struct HeroSizeHold { Identity display;double progress=0,applied=1,clock=0;bool want=false;uint32_t logs=0; };
HeroSizeHold hero_size_;
bool SetDisplayScale(Object* display,double s){return Call(display,"SetRelativeScale3D").Set("NewScale3D",Vector{s,s,s}).Invoke();}
void ResetHeroSize(const char* why){
    if(Same(hero_size_.display)&&std::abs(hero_size_.applied-1)>1e-6){
        const bool ok=SetDisplayScale(hero_size_.display.object,1);
        Logf("NPC_SIZE %s: %s back to its own size (was x%.3f) ok=%d",why,Text(hero_size_.display.object).c_str(),hero_size_.applied,ok?1:0);}
    hero_size_.display={};hero_size_.applied=1;hero_size_shown_.store(1.0,std::memory_order_relaxed);
}
// The game camera's lift (ApplyCameraLift: raised by the look's head over the hero's) for the size 主角体型 takes the
// look to: the shown head over the hero's, none at the hero's size (彭猛 kept 252 cm over a hero-sized model). The
// camera blends there (SetCameraLift, 0.4 s); HoldCameraLift keeps it; the look's return resets it as before.
void FitCameraLift(double shown){
    if(!camera_owned_||!Same(camera_))return;
    const double lift=std::clamp(NpcHeroSize::LiftAt(camera_lift_,look_ratio_,shown),kCameraLiftLow,kCameraLiftHigh);
    if(!std::isfinite(lift)||std::abs(lift-camera_applied_)<1||!SetCameraLift(camera_.object,lift))return;
    if(hero_size_.logs<400){++hero_size_.logs;Logf("NPC_CAMERA lift %.1f -> %.1fcm for the look shown x%.3f (主角体型)",camera_applied_,lift,shown);}
    camera_applied_=lift;
}
// The look's cloth and hair once the size has settled (the end of an ease): started again from the pose at the new size
// rather than simulated on from particles laid out at the old one.
void ResetLookSimulation(double s){
    Object* cloth=Class("/Script/ChaosClothAssetEngine.ChaosClothComponent"),*groom=Class("/Script/HairStrandsCore.GroomComponent");
    int cloths=0,grooms=0;
    for(const Identity& part:added_){if(!Same(part)||part.object==display_.object)continue;
        if(cloth&&ue::IsA(part.object,cloth)&&Call(part.object,"ForceNextUpdateTeleportAndReset").Invoke())++cloths;
        else if(groom&&ue::IsA(part.object,groom)&&Call(part.object,"ResetSimulation").Invoke())++grooms;}
    if((cloths||grooms)&&hero_size_.logs<400){++hero_size_.logs;Logf("NPC_SIZE settled at x%.3f: cloth %d and hair %d started again at that size",s,cloths,grooms);}
}
void DriveHeroSize(){
    const double clock=ClockSeconds(),dt_ms=hero_size_.clock>0?(std::min)(0.1,clock-hero_size_.clock)*1000:0;hero_size_.clock=clock;
    using P=ComboRun::Phase;
    const bool shown=active_&&Same(display_)&&!cutscene_host_;
    // DriveCombo ran ComboReady this frame (a chain that is off or has no owner is reset there).
    const bool move=combo_.config.enabled&&combo_.trigger>=0&&combo_.phase!=P::Idle&&combo_.playing>=0&&!PlayingAssist()&&!native_handoff_;
    const bool want=shown&&move&&NpcCombo::HeroSizeFor(combo_.config,combo_.trigger,combo_.playing);
    if(want!=hero_size_.want){hero_size_.want=want;
        if(hero_size_.logs<400){++hero_size_.logs;Logf("NPC_SIZE %s node=%d: look height ratio %.3f, shown x%.3f",want?"on":"off",combo_.playing,look_ratio_,want?NpcHeroSize::Target(look_ratio_):1.0);}}
    if(!shown){ResetHeroSize("no look shown");hero_size_.progress=0;return;}
    if(!Same(hero_size_.display)||hero_size_.display.object!=display_.object){ResetHeroSize("another display");hero_size_.display=display_;}
    hero_size_.progress=NpcHeroSize::Step(hero_size_.progress,want,dt_ms);
    FitCameraLift(want?NpcHeroSize::Target(look_ratio_):1.0);
    const double s=NpcHeroSize::Shown(hero_size_.progress,NpcHeroSize::Target(look_ratio_));
    if(std::abs(s-hero_size_.applied)<1e-4)return;
    if(SetDisplayScale(display_.object,s)){hero_size_.applied=s;hero_size_shown_.store(s,std::memory_order_relaxed);
        if(hero_size_.progress<=0||hero_size_.progress>=1)ResetLookSimulation(s);}
}
// Any montage a boss look plays (the look's own attacks, a move cast some other way than the chain or the NPC key) is a
// cast too: keyed by the montage (0: none).
uint64_t LookMontage(Object* mesh){
    if(!SkinOn()||!ue::Alive(mesh))return 0;
    Call instance(mesh,"GetAnimInstance");Object* anim=instance.Invoke()?instance.Get<Object*>("ReturnValue"):nullptr;if(!ue::Alive(anim))return 0;
    Call active(anim,"GetCurrentActiveMontage");Object* montage=active.Invoke()?active.Get<Object*>("ReturnValue"):nullptr;
    return ue::Alive(montage)?reinterpret_cast<uint64_t>(montage):0;
}
void DriveViewFollow(){
    const NpcCombo::Node* n=combo_.playing>=0&&static_cast<size_t>(combo_.playing)<combo_.config.chain.size()?&combo_.config.chain[static_cast<size_t>(combo_.playing)]:nullptr;
    const bool chain_move=n&&NpcCombo::IsMove(*n)&&n->type!=NpcCombo::NodeType::Assist&&n->type!=NpcCombo::NodeType::Plugin;  // a 援护 is another actor; a 插件节点 plays nothing
    const int64_t handle=skill_.handle>0?static_cast<int64_t>(skill_.handle):0;
    Context context;const bool have=ContextNow(context)&&ue::Alive(context.pawn);
    const uint64_t look_montage=!chain_move&&handle<=0&&have?LookMontage(context.mesh):0;
    const bool want=view_enabled_&&have&&(chain_move||handle>0||look_montage);
    const ULONGLONG now=GetTickCount64();
    if(want)view_cam_.wanted_at=now;
    else EndViewFollow(view_enabled_?"the move is over":"switched off");
    if(!have||(!view_enabled_&&!view_cam_.on)){ReleaseViewCam(have?"switched off":"no pawn");return;}
    if(view_cam_.on&&(!Same(view_cam_.pawn)||view_cam_.pawn.object!=context.pawn))ReleaseViewCam("another pawn");
    // 镜头跟随 changed: the relation was to the other bone; ours lets go, the game camera's relation is measured again.
    static int target_seen=-1;
    const int bone=NpcCombo::ViewTarget();
    if(bone!=target_seen){
        if(target_seen>=0){view_native_.known=false;ReleaseViewCam("the camera's bone changed");Logf("NPC_VIEW camera looks at the %s now",
            bone==NpcCombo::kViewHead?"head":bone==NpcCombo::kViewNeck?"neck":"pelvis");}
        target_seen=bone;
    }
    Object* pc=PlayerController(context.pawn);if(!pc)return;
    Object* mesh=Same(display_)?display_.object:context.mesh;if(!ue::Alive(mesh))return;
    Vector body{};if(!ViewBodyPoint(mesh,body))return;
    // The game camera now: its eye and rotation (its relation to the bone), its field of view (ours takes it).
    Call manager(ue::Default("/Script/Engine.Default__GameplayStatics"),"GetPlayerCameraManager");
    Object* cam=manager.Set("WorldContextObject",context.pawn).Set("PlayerIndex",int32_t{0}).Invoke()?manager.Get<Object*>("ReturnValue"):nullptr;
    if(!ue::Alive(cam))return;
    Vector eye_now{};{Call at(cam,"GetCameraLocation");if(!at.Invoke())return;eye_now=at.Get<Vector>("ReturnValue");}
    float fov=90;{Call f(cam,"GetFOVAngle");if(f.Invoke())fov=f.Get<float>("ReturnValue");}
    RotatorD seen{};{Call r(cam,"GetCameraRotation");if(r.Invoke())seen=r.Get<RotatorD>("ReturnValue");}
    Object* target_now=ViewTargetOf(pc);
    // The game set another view (a cinematic): let go without touching it; taken over again only from the game's camera.
    // (While a blend runs the view target is still the one it leaves: not checked then.)
    if(view_cam_.on&&now>=view_cam_.blend_until&&target_now!=view_cam_.actor.object){
        if(target_now==context.pawn){view_cam_.on=false;view_cam_.placed=false;view_cam_.released_at=now;Logf("NPC_VIEW camera: the game took the view back to its own camera");}
        else{view_cam_.on=false;view_cam_.placed=false;view_cam_.released_at=now;Logf("NPC_VIEW camera: the game set another view (%s); let go",Text(target_now).c_str());return;}
    }
    const bool native_view=!view_cam_.on&&target_now==context.pawn;
    const bool leaving=!view_cam_.on&&Same(view_cam_.actor)&&target_now==view_cam_.actor.object;  // blending back out: taken again at once
    const bool settled=now>=view_cam_.released_at+static_cast<ULONGLONG>(kCamOutS*1000)+300;  // a blend back is over
    // The game camera's relation to the bone, while it is the view and settled (between moves).
    if(native_view&&settled){
        const Vector rel_now=ViewFollowState::ToLocal(ViewFollowState::ViewAxes<Vector>(seen.yaw,seen.pitch),Vector{eye_now.x-body.x,eye_now.y-body.y,eye_now.z-body.z});
        const double len=ViewFollowState::Length(rel_now);
        if(std::isfinite(len)&&len>120&&len<1500){
            const bool fresh=!view_native_.known||!Same(view_native_.pawn)||view_native_.pawn.object!=context.pawn||view_native_.target!=bone;
            if(fresh){
                view_native_={};view_native_.pawn=Id(context.pawn);view_native_.target=bone;view_native_.rel=rel_now;view_native_.known=true;
                Logf("NPC_VIEW camera: the game camera frames the %s of %s from (%.0f,%.0f,%.0f) in its axes, %.0fcm",view_body_.how.c_str(),Text(mesh).c_str(),rel_now.x,rel_now.y,rel_now.z,len);
            }else{
                const double clock=ClockSeconds();static double last=0;const double dt=last>0?(std::min)(0.1,clock-last):0;last=clock;
                const double a=dt>0?1-std::exp(-dt/kNativeS):0;Vector& r=view_native_.rel;
                r={r.x+(rel_now.x-r.x)*a,r.y+(rel_now.y-r.y)*a,r.z+(rel_now.z-r.z)*a};
            }
        }
    }
    if(!want&&(!view_cam_.on||now>view_cam_.wanted_at+kCamGraceMs)){ReleaseViewCam("no move");return;}
    if(!view_cam_.on&&!native_view&&!leaving)return;
    if(!view_cam_.on&&!view_native_.known&&(leaving||!settled))return;  // measured first: the game camera settles back
    Vector actor{};if(!ActorAt(context.pawn,actor))return;
    const double yaw=ActorYaw(context.pawn);constexpr double kRad=3.14159265358979323846/180.0;
    // The follow's bookkeeping (one line per move): where the body sits on the actor when it starts (the actor's axes).
    const int node=chain_move?combo_.playing:-1;
    const uint64_t cast_start=look_montage?look_montage:skill_.started;
    if(want){
        if(view_.on&&(!Same(view_.pawn)||view_.pawn.object!=context.pawn))EndViewFollow("another pawn");
        else if(view_.on&&!ViewFollowState::SameCast(view_.node,view_.visit,view_.handle,view_.cast_start,node,combo_.visit,handle,cast_start))EndViewFollow("the next cast");
        const auto rebase=[&]{const double c0=std::cos(-yaw*kRad),s0=std::sin(-yaw*kRad),bx=body.x-actor.x,by=body.y-actor.y;
                              view_.base={bx*c0-by*s0,bx*s0+by*c0,body.z-actor.z};view_.mesh=Id(mesh);};
        if(!view_.on){
            view_.on=true;view_.node=node;view_.visit=combo_.visit;view_.handle=handle;view_.cast_start=cast_start;view_.pawn=Id(context.pawn);view_.started=now;rebase();
            view_.move=n&&chain_move?"node="+std::to_string(node)+" "+(n->boss.empty()?std::string():n->boss+" ")+n->ability:
                       handle>0?"skill "+skill_.name:"look montage "+Text(reinterpret_cast<Object*>(look_montage));
            view_.move+=" (body: "+view_body_.how+")";
        }else if(!Same(view_.mesh)||view_.mesh.object!=mesh)rebase();
        if(handle>0){view_.handle=handle;view_.cast_start=skill_.started;}
    }
    const double c=std::cos(yaw*kRad),s=std::sin(yaw*kRad);
    const double dx=body.x-actor.x-(view_.base.x*c-view_.base.y*s),dy=body.y-actor.y-(view_.base.x*s+view_.base.y*c),dz=body.z-actor.z-view_.base.z;
    if(view_.on){view_.max_flat=(std::max)(view_.max_flat,std::sqrt(dx*dx+dy*dy));view_.max_rise=(std::max)(view_.max_rise,std::abs(dz));}
    RotatorD control{};{Call get(pc,"GetControlRotation");if(!get.Invoke())return;control=get.Get<RotatorD>("ReturnValue");}
    const auto axes=ViewFollowState::ViewAxes<Vector>(control.yaw,control.pitch);
    // The relation: the game camera's (else the hero's default), pushed out for a model taller than the hero (镜头取景
    // 按身高), or by the shown model's height over the hero's (按体型比例: 1 under 主角体型, under 1 for a small look).
    const double size=ViewModelSize(mesh);
    const Vector native=view_native_.known?view_native_.rel:CamDefault(bone);
    const bool by_ratio=NpcCombo::ViewScale()==NpcCombo::kScaleHeroRatio,look_shown=Same(display_)&&mesh==display_.object;
    const double grow=by_ratio?(look_shown?NpcHeroSize::Grow(look_ratio_,hero_size_.applied,size/kHeroCm):1.0):(std::max)(1.0,size/kHeroCm);
    const Vector rel{native.x*grow,native.y*grow,native.z*grow};
    // The target: the body through the dead zone, eased; at once when the camera comes in or after a pose jump.
    const double clock=ClockSeconds(),dt=view_cam_.clock>0?(std::min)(0.1,clock-view_cam_.clock):0;view_cam_.clock=clock;
    if(!view_cam_.placed||now<view_cam_.snap_until)view_cam_.target=body;
    else{
        const Vector d{body.x-view_cam_.target.x,body.y-view_cam_.target.y,body.z-view_cam_.target.z};Vector goal=view_cam_.target;
        const double flat=std::sqrt(d.x*d.x+d.y*d.y);
        if(flat>kCamDeadFlat){const double k=(flat-kCamDeadFlat)/flat;goal.x+=d.x*k;goal.y+=d.y*k;}
        if(std::abs(d.z)>kCamDeadRise)goal.z+=d.z-(d.z>0?kCamDeadRise:-kCamDeadRise);
        const double a=dt>0?1-std::exp(-dt/kCamFollowS):0;
        view_cam_.target={view_cam_.target.x+(goal.x-view_cam_.target.x)*a,view_cam_.target.y+(goal.y-view_cam_.target.y)*a,view_cam_.target.z+(goal.z-view_cam_.target.z)*a};
    }
    const Vector& t=view_cam_.target;
    const Vector way=ViewFollowState::LookDownAtLeast(ViewFollowState::ToWorld(axes,rel),kCamMinDown);
    const double dist=ViewFollowState::Length(way);if(dist<1)return;
    // Walls: in front of a hit at once, back out eased.
    double allowed=dist;
    if(view_cam_.skip.empty()||now>=view_cam_.skip_at){view_cam_.skip=TraceIgnore(context.pawn,nullptr);view_cam_.skip_at=now+kViewSkipRefreshMs;
        if(Same(view_cam_.actor))view_cam_.skip.push_back(view_cam_.actor.object);}
    for(int tries=0;tries<4;++tries){
        Vector hit{};Object* blocker=nullptr;
        if(TraceLine(context.pawn,t,Vector{t.x+way.x,t.y+way.y,t.z+way.z},view_cam_.skip,&hit,nullptr,&blocker)!=1)break;
        if(ViewPassThrough(blocker,context.pawn)){view_cam_.skip.push_back(blocker);if(view_.on)++view_.passed;continue;}
        allowed=(std::max)(kCamNearCm,Gap3(t,hit)-kCamWallGapCm);
        if(view_.on){++view_.wall_frames;view_.blocker=blocker?Text(blocker):std::string("world");}
        break;
    }
    if(view_cam_.wall<0||!view_cam_.placed||allowed<view_cam_.wall)view_cam_.wall=allowed;
    else view_cam_.wall+=(allowed-view_cam_.wall)*(dt>0?1-std::exp(-dt/kCamWallOutS):0);
    const double k=(std::min)(view_cam_.wall,dist)/dist;
    const Vector eye{t.x+way.x*k,t.y+way.y*k,t.z+way.z*k};
    const RotatorD rot{control.pitch,control.yaw,0};
    if(view_.on){view_.size=size;view_.dist=dist;view_.min_wall=view_.wall_frames?(view_.min_wall>0?(std::min)(view_.min_wall,dist*k):dist*k):0;}
    // In: our camera at its place, then the view blended to it.
    if(!view_cam_.on){
        if(!want)return;
        Object* actor_cam=ViewCamActor(context.pawn);if(!actor_cam)return;
        Call(actor_cam,"K2_SetActorLocationAndRotation").Set("NewLocation",eye).Set("NewRotation",rot).Set("bSweep",false).Set("bTeleport",true).Invoke();
        if(Same(view_cam_.component))Call(view_cam_.component.object,"SetFieldOfView").Set("InFieldOfView",fov).Invoke();
        if(!BlendViewTo(pc,actor_cam,kCamInS)){Logf("NPC_VIEW camera: SetViewTargetWithBlend refused");return;}
        view_cam_.on=true;view_cam_.placed=true;view_cam_.pawn=Id(context.pawn);view_cam_.blend_until=now+static_cast<ULONGLONG>(kCamInS*1000)+150;
        Logf("NPC_VIEW camera took over for %s: model %.0fcm tall (shown x%.3f), from (%.0f,%.0f,%.0f) x%.2f = %.0fcm (the game camera's%s), fov %.0f, control pitch/yaw %.0f/%.0f, game view %.0f/%.0f",
             view_.move.c_str(),size,ShownScale(mesh),native.x,native.y,native.z,grow,dist,view_native_.known?"":": not measured, the hero's default",fov,control.pitch,control.yaw,seen.pitch,seen.yaw);
        if(by_ratio)Logf("NPC_VIEW framing by size ratio: look %.3f x shown %.3f -> x%.2f",look_shown?look_ratio_:1.0,look_shown?hero_size_.applied:1.0,grow);
        return;
    }
    Call(view_cam_.actor.object,"K2_SetActorLocationAndRotation").Set("NewLocation",eye).Set("NewRotation",rot).Set("bSweep",false).Set("bTeleport",true).Invoke();
    view_cam_.placed=true;
    // How far off the view centre the body is (log only).
    if(!view_.on)return;
    const Vector f=axes.f,to{body.x-eye.x,body.y-eye.y,body.z-eye.z};const double reach=ViewFollowState::Length(to);if(reach<1)return;
    const double angle=std::acos(std::clamp(ViewFollowState::Dot(f,to)/reach,-1.0,1.0))/kRad;
    view_.max_angle=(std::max)(view_.max_angle,angle);++view_.frames;if(angle>30)++view_.off_frames;
    if(now>=view_.trace_at&&view_.trace_n<24){
        const double side=ViewFollowState::Dot(axes.r,to),up=ViewFollowState::Dot(axes.u,to),fwd=ViewFollowState::Dot(f,to);
        view_.trace_at=now+100;++view_.trace_n;char sample[64]{};
        _snprintf_s(sample,sizeof(sample),_TRUNCATE," %.1f:%.0f,%.0f,%.0f/%.0f",(now-view_.started)/1000.0,dz,eye.z-t.z,std::atan2(up,fwd)/kRad,std::atan2(side,fwd)/kRad);
        view_.trace+=sample;
    }
}
void BeforeViewTrimSeek(double position,double destination){
    if(!view_enabled_)return;
    if(!ViewFollowState::IsPoseJump(position,destination))return;
    // The target takes the jumped pose at once for kViewSnapMs (not eased behind it).
    DriveViewFollow();if(!view_.on)return;
    ++view_.seeks;view_cam_.snap_until=GetTickCount64()+kViewSnapMs;
    Logf("NPC_VIEW trim seek node=%d visit=%llu from=%.3f to=%.3f: the camera's target jumps with it",view_.node,view_.visit,position,destination);
}
// Tests: npcretarget lockstruct: SetLockTarget's reflected Target struct, the combat system's lock / target structs and the
// soft-lock picks as bytes (bytes 8..15 also read as a name). Log only.
volatile LONG lock_struct_probe_=0,cam_offset_test_=0;Vector cam_offset_value_{};
void ProbeLockStruct(Object* pawn){
    Object* targeting=TargetingComponent(pawn);
    if(!targeting){Logf("NPC_TEST lockstruct: no targeting component");return;}
    Object* fn=ue::FindFunction(ue::ClassOf(targeting),"SetLockTarget");
    ue::Field params[8];const size_t n=fn?ue::Fields(fn,params,std::size(params)):0;
    std::string text;for(size_t i=0;i<n;++i)text+=std::string(" ")+params[i].name+"@"+std::to_string(params[i].offset)+"["+std::to_string(params[i].size)+"]";
    Logf("NPC_TEST lockstruct SetLockTarget on %s params:%s",Text(ue::ClassOf(targeting)).c_str(),text.c_str());
    Object* script_struct=Class("/Script/CoreUObject.ScriptStruct");int listed=0;
    if(script_struct)ue::ForEachObject([&](Object* o){
        if(ue::ClassOf(o)!=script_struct)return true;
        char name[128]{};ue::NameText(ue::NameOf(o),name,sizeof(name));
        if(!std::strstr(name,"Target")&&!std::strstr(name,"Lock"))return true;
        char pkg[128]{};ue::NameText(ue::NameOf(ue::OuterOf(o)),pkg,sizeof(pkg));
        if(!std::strstr(pkg,"SGame")&&!std::strstr(pkg,"Target"))return true;
        ue::Field f[16];const size_t m=ue::Fields(o,f,std::size(f));std::string fields;
        for(size_t i=0;i<m;++i)fields+=std::string(" ")+f[i].name+"@"+std::to_string(f[i].offset)+"["+std::to_string(f[i].size)+"]";
        Logf("NPC_TEST lockstruct %s.%s size=%d:%s",pkg,name,ue::StructSize(o),fields.c_str());
        return ++listed<60;});
    static const Name soft=ue::MakeName(L"Targeting.Handler.CombatSoftLock");
    for(int keep=0;keep<2;++keep){Call find(targeting,"FindTargetByHandler");
        if(soft.None()||!find.Set("HandlerTag",soft).Set("UseLockTarget",keep==1).Invoke()||!find.Get<bool>("ReturnValue")){Logf("NPC_TEST lockstruct pick (lock kept %d): none",keep);continue;}
        LockTarget t=find.Get<LockTarget>("Target");char hex[3*16+1]{};for(int i=0;i<16;++i)std::snprintf(hex+3*i,4,"%02x ",t.bytes[i]);
        const Name at8{ue::At<uint32_t>(t.bytes,8),ue::At<uint32_t>(t.bytes,12)};char n8[96]{};ue::NameText(at8,n8,sizeof(n8));
        Object* actor=ue::At<Object*>(t.bytes,0);
        Logf("NPC_TEST lockstruct pick (lock kept %d): %s pointer at 0=%s bytes 8..15 as a name=%s",keep,hex,ue::Alive(actor)?Text(actor).c_str():"(not an object)",n8);}
}
// Tests: npcretarget viewprobe <ms>: every 200 ms, the actor, the visible model (the NPC look's display, else the hero's
// mesh: the component, bone 0 and bone 1, horizontal cm from the actor), the hero mesh's own bone 0 under a look, the
// camera and how far bone 1 is off the view centre. Log only.
volatile LONG view_probe_ms_=0;ULONGLONG view_probe_until_=0,view_probe_next_=0;
void DriveViewProbe(){
    if(const LONG ms=InterlockedExchange(&view_probe_ms_,0))view_probe_until_=GetTickCount64()+static_cast<ULONGLONG>(ms);
    const ULONGLONG now=GetTickCount64();if(!view_probe_until_)return;
    if(now>=view_probe_until_){view_probe_until_=0;Logf("NPC_VIEW probe off");return;}
    if(now<view_probe_next_)return;view_probe_next_=now+200;
    Context context;if(!ContextNow(context)||!ue::Alive(context.pawn))return;
    Vector actor{};ActorAt(context.pawn,actor);
    Object* mesh=Same(display_)?display_.object:context.mesh;
    const auto bone=[&](Object* m,int32_t i,Vector& v,std::string& name){
        if(!ue::Alive(m))return false;Call get(m,"GetBoneName");if(!get.Set("BoneIndex",i).Invoke())return false;const Name bn=get.Get<Name>("ReturnValue");if(bn.None())return false;
        char t[64]{};ue::NameText(bn,t,sizeof(t));name=t;Call at(m,"GetSocketLocation");if(!at.Set("InSocketName",bn).Invoke())return false;v=at.Get<Vector>("ReturnValue");return true;};
    Vector root{},one{},comp{},hero_root{};std::string rn="-",n1="-",hrn="-";
    const bool r=bone(mesh,0,root,rn),o=bone(mesh,1,one,n1),hr=mesh!=context.mesh&&bone(context.mesh,0,hero_root,hrn);
    {Call c(mesh,"K2_GetComponentLocation");if(c.Invoke())comp=c.Get<Vector>("ReturnValue");}
    Call manager(ue::Default("/Script/Engine.Default__GameplayStatics"),"GetPlayerCameraManager");
    Object* cam=manager.Set("WorldContextObject",context.pawn).Set("PlayerIndex",int32_t{0}).Invoke()?manager.Get<Object*>("ReturnValue"):nullptr;
    Vector eye{};RotatorD view{};
    if(ue::Alive(cam)){Call l(cam,"GetCameraLocation");if(l.Invoke())eye=l.Get<Vector>("ReturnValue");Call ro(cam,"GetCameraRotation");if(ro.Invoke())view=ro.Get<RotatorD>("ReturnValue");}
    constexpr double kRad=3.14159265358979323846/180.0;
    const Vector fwd{std::cos(view.pitch*kRad)*std::cos(view.yaw*kRad),std::cos(view.pitch*kRad)*std::sin(view.yaw*kRad),std::sin(view.pitch*kRad)};
    const Vector aim=o?one:root;const Vector to{aim.x-eye.x,aim.y-eye.y,aim.z-eye.z};const double len=std::sqrt(to.x*to.x+to.y*to.y+to.z*to.z);
    const double off=len>1?std::acos((std::max)(-1.0,(std::min)(1.0,(to.x*fwd.x+to.y*fwd.y+to.z*fwd.z)/len)))/kRad:-1;
    const auto flat=[&](const Vector& a){return std::sqrt((a.x-actor.x)*(a.x-actor.x)+(a.y-actor.y)*(a.y-actor.y));};
    Logf("NPC_VIEW actor=(%.0f,%.0f,%.0f) model=%s comp+%.0f %s+%.0f(z%+.0f) %s+%.0f(z%+.0f) heroBone0 %s+%.0f camera=(%.0f,%.0f,%.0f) yaw=%.0f pitch=%.0f %.0fcm from bone1, %.0f deg off centre skill=%s chainNode=%d",
         actor.x,actor.y,actor.z,mesh==context.mesh?"hero-mesh":"npc-display",flat(comp),rn.c_str(),r?flat(root):-1.0,root.z-actor.z,n1.c_str(),o?flat(one):-1.0,one.z-actor.z,
         hrn.c_str(),hr?flat(hero_root):-1.0,eye.x,eye.y,eye.z,view.yaw,view.pitch,len,off,skill_.handle>0?skill_.name.c_str():"-",combo_.playing);
}
// 钩子: its GA hooks the lock target, so the lock goes on it: the lock as it is (also one that cannot be read), else the
// nearest enemy within kTeleportRangeCm, locked now.
struct HookProbe { Identity hero,target;Vector hero_at{},target_at{};ULONGLONG at=0;int node=-1,step=0; };
HookProbe hook_probe_;
void DriveHookProbe(){
    if(!hook_probe_.at||hook_probe_.step>=2)return;
    const ULONGLONG wait=hook_probe_.step==0?800:2000;
    if(GetTickCount64()<hook_probe_.at+wait)return;
    ++hook_probe_.step;
    Vector h{},t{};const bool hero=Same(hook_probe_.hero)&&ActorAt(hook_probe_.hero.object,h),target=Same(hook_probe_.target)&&ActorAt(hook_probe_.target.object,t);
    const auto gap=[](const Vector& a,const Vector& b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));};
    Logf("NPC_COMBO hook node=%d %llums after the cast: distance=%.0fcm hero moved %.0fcm target moved %.0fcm",hook_probe_.node,wait,hero&&target?gap(h,t):-1.0,
         hero?gap(h,hook_probe_.hero_at):-1.0,target?gap(t,hook_probe_.target_at):-1.0);
    if(hook_probe_.step>=2)hook_probe_={};
}
// 钩子 closes in: the game's hook leaves a pulled target about 3.5 m from the hero and the hero ~1.2 m from the one it
// flew to, and a target already within ~4 m is not pulled at all (near, 勾过来 lunges the hero in and knocks the target
// off, 11-08). So the hook node carries one of them on until the capsules are kHookGapCm apart, as one motion with the
// game's. 勾过来: the target's speed toward the hero is read every frame, and once the game's pull slows down (under
// kHookTakeOver of its peak) the target is carried on from that speed, slowing to a stop at the gap (a cubic from its
// speed to rest over kHookCarryMin..kHookCarryMax s); a target the hook does not pull (within kHookNearCm at the
// cast) is pulled in from rest when the pull would have started (kHookConnectMs after the cast), a far one the game has
// not pulled by kHookPullWaitMs likewise (its pull starts later the farther it is: 10 m, not yet at 0.66 s). The hero's own steps in the throw are not a pull
// (they set it off 0.2 s in and carried the hero, 11-08). 勾过去's flight is left to the game; with no such motion it
// eases on from rest once the hook pulled one of them and both are still for kHookStillMs, else once the hook's own
// move is over. A next node that starts first: 勾过来 eases the target on at
// once, 勾过去 is left. The carried one never goes back against the game's own motion (it is only moved where the
// curve is ahead of it), its goal follows the other one, and each frame is swept: a step up as a character takes it
// (kHookStepCm), walls and the other's capsule stop it (tried again for kHookStuckMs). Given up kHookCloseWaitMaxMs
// after the cast. (11-08 round 2: the slide used to start 90 ms after the pull had stopped and cover up to 2.5 m in
// 0.15 s, a second move after the first.)
constexpr double kHookGapCm=35,kHookStillCm=2,kHookStepCm=45,kHookTakeOver=0.6,kHookPeakMin=300,kHookCarryMin=0.16,kHookCarryMax=0.5;
constexpr ULONGLONG kHookSettleMinMs=450,kHookStillMs=90,kHookCloseWaitMaxMs=5000,kHookStuckMs=200,kHookConnectMs=650,kHookPullWaitMs=1600;constexpr double kHookNearCm=450;
struct HookClose { Identity hero,target,mover,other;int node=-1,phase=0,stepped=0,frames=0;bool self=false,seen=false;double start_gap=0;
                   ULONGLONG cast=0,still_since=0,blocked_since=0,pull_at=0;double clock=0,slide_at=0,length=0,v0=0,dist=0,peak=0,hero_v=0,target_v=0,target_peak=0;
                   Vector hero_at{},target_at{},hero_last{},target_last{},from{},dir{};std::string ability,how; };
HookClose hook_close_;
double HookClock(){return ClockSeconds();}
// What stopped a swept move (K2_SetActorLocation's SweepHitResult read through GameplayStatics.BreakHitResult).
std::string SweepHit(Object* mover,Call& move){
    const int32_t size=ParamSize(ue::FindFunction(ue::ClassOf(mover),"K2_SetActorLocation"),"SweepHitResult");
    unsigned char* hit=size>0?move.Slot("SweepHitResult",static_cast<size_t>(size)):nullptr;if(!hit)return "hit unread";
    Call brk(ue::Default("/Script/Engine.Default__GameplayStatics"),"BreakHitResult");
    unsigned char* in=brk.Slot("Hit",static_cast<size_t>(size));if(!in)return "hit unread (no BreakHitResult)";
    std::memcpy(in,hit,static_cast<size_t>(size));if(!brk.Invoke())return "hit unread (break failed)";
    char text[400]{};const Vector normal=brk.Get<Vector>("ImpactNormal");
    _snprintf_s(text,sizeof(text),_TRUNCATE,"%s%s: actor=%s component=%s at %.0fcm normal=(%.2f,%.2f,%.2f)",brk.Get<bool>("bBlockingHit")?"blocking hit":"no blocking hit",
                brk.Get<bool>("bInitialOverlap")?" (it started inside it)":"",Text(brk.Get<Object*>("HitActor")).c_str(),Text(brk.Get<Object*>("HitComponent")).c_str(),
                static_cast<double>(brk.Get<float>("Distance")),normal.x,normal.y,normal.z);
    return text;
}
double HookWant(const HookClose& h){double hh=0,hr=34,th=0,tr=34;CapsuleOf(h.hero.object,hh,hr);CapsuleOf(h.target.object,th,tr);return (std::max)(hr,20.0)+(std::max)(tr,20.0)+kHookGapCm;}
// The carry from a (the hero) and b (the target) now: `hero` moves (else the target), from speed v (cm/s toward the
// other, 0: from rest). False (and the close-in over) when they are near enough already.
bool StartHookSlide(HookClose& h,const Vector& a,const Vector& b,bool hero,double v,const char* why){
    const ULONGLONG now=GetTickCount64();
    const double hero_moved=Gap3(a,h.hero_at),target_moved=Gap3(b,h.target_at),want=HookWant(h);
    const double dx=b.x-a.x,dy=b.y-a.y,flat=std::sqrt(dx*dx+dy*dy);
    if(flat<=want+10||flat<1){Logf("NPC_COMBO hook close node=%d %s %llums after the cast: already %.0fcm apart (want %.0f), hero moved %.0fcm target moved %.0fcm",
                                    h.node,why,now-h.cast,flat,want,hero_moved,target_moved);h={};return false;}
    h.mover=hero?h.hero:h.target;h.other=hero?h.target:h.hero;h.from=hero?a:b;
    h.dir=hero?Vector{-dx/flat,-dy/flat,0}:Vector{dx/flat,dy/flat,0};  // from the other one toward the mover: its goal is `want` out along it
    h.dist=flat-want;h.v0=(std::max)(0.0,v);
    h.length=std::clamp(h.dist/650.0+0.1,kHookCarryMin,kHookCarryMax);
    if(h.v0>0)h.length=std::clamp((std::min)(h.length,2.4*h.dist/h.v0),kHookCarryMin,kHookCarryMax);  // a fast one stops sooner
    h.length/=std::clamp(NpcCombo::HookPace(combo_.config,h.node),0.5,3.0);  // the node's 钩子速度
    h.slide_at=HookClock();h.phase=2;h.frames=0;h.how=h.v0>0?"carried on":"eased on";
    Logf("NPC_COMBO hook close node=%d %s %llums after the cast (hero moved %.0fcm, target %.0fcm; peak %.0fcm/s): the %s is %s from %.0fcm/s over %.0fcm in %.2fs, %.0f -> %.0f cm apart",
         h.node,why,now-h.cast,hero_moved,target_moved,h.peak,hero?"hero":"target",h.how.c_str(),h.v0,h.dist,h.length,flat,want);
    return true;
}
// The carry's progress (0..1) at u (0..1 of its time): a cubic from the start speed to rest (Hermite: end slope 0).
double HookProgress(const HookClose& h,double u){
    u=std::clamp(u,0.0,1.0);const double m=h.dist>1?(std::min)(3.0,h.v0*h.length/h.dist):0;
    return (3*u*u-2*u*u*u)+m*(u*u*u-2*u*u+u);
}
void DriveHookClose(){
    HookClose& h=hook_close_;if(!h.phase)return;
    const ULONGLONG now=GetTickCount64();
    if(!Same(h.hero)||!Same(h.target)){Logf("NPC_COMBO hook close node=%d skipped: the hero or the target is gone",h.node);h={};return;}
    Vector a{},b{};if(!ActorAt(h.hero.object,a)||!ActorAt(h.target.object,b)){h={};return;}
    if(h.phase==1){
        const double clock=HookClock(),dt=h.clock>0?clock-h.clock:0;h.clock=clock;
        const double dx=b.x-a.x,dy=b.y-a.y,flat=std::sqrt(dx*dx+dy*dy);
        // Speeds toward each other (cm/s, smoothed over a few frames).
        if(dt>0.002&&flat>1){
            const double ux=dx/flat,uy=dy/flat;
            const double hv=((a.x-h.hero_last.x)*ux+(a.y-h.hero_last.y)*uy)/dt,tv=-((b.x-h.target_last.x)*ux+(b.y-h.target_last.y)*uy)/dt;
            h.hero_v=0.5*h.hero_v+0.5*hv;h.target_v=0.5*h.target_v+0.5*tv;
            h.peak=(std::max)(h.peak,h.hero_v+h.target_v);h.target_peak=(std::max)(h.target_peak,h.target_v);
            if(h.target_v>=kHookPeakMin&&!h.pull_at){h.pull_at=now;Logf("NPC_COMBO hook close node=%d the pull starts %llums after the cast (%.0fcm apart)",h.node,now-h.cast,flat);}}
        const bool still=Gap3(a,h.hero_last)<kHookStillCm&&Gap3(b,h.target_last)<kHookStillCm;h.hero_last=a;h.target_last=b;
        if(!still)h.still_since=0;else if(!h.still_since)h.still_since=now;
        const double need=(std::min)(150.0,0.4*h.start_gap);
        const bool pulled=h.start_gap-flat>=need&&(h.self?Gap3(a,h.hero_at):Gap3(b,h.target_at))>=need;
        const bool running=skill_.handle>0&&skill_.name==h.ability;if(running)h.seen=true;
        const bool over=h.seen&&!running;
        const bool hero_moves=h.self||Gap3(a,h.hero_at)>Gap3(b,h.target_at)+100;  // 勾过来: the hero only when it was the one carried (a lunge, a heavy target)
        if(combo_.playing>=0&&combo_.playing!=h.node){
            if(h.self){Logf("NPC_COMBO hook close node=%d skipped: another node plays",h.node);h={};return;}
            StartHookSlide(h,a,b,false,(std::max)(0.0,h.target_v),"the next node starts:");return;}
        // 勾过来: the game's pull is taken over as it slows down; a target it does not pull is pulled in when it would.
        if(!h.self&&flat>HookWant(h)+15){
            if(h.target_peak>=kHookPeakMin&&h.target_v<kHookTakeOver*h.target_peak){StartHookSlide(h,a,b,false,h.target_v,"the pull slows down:");return;}
            if(!h.pull_at&&h.seen&&now>=h.cast+(h.start_gap<=kHookNearCm?kHookConnectMs:kHookPullWaitMs)){
                StartHookSlide(h,a,b,false,0,h.start_gap<=kHookNearCm?"near: no pull by the time it connects, pulled in":"far: not pulled yet, pulled in");return;}
        }
        if(now<h.cast+kHookSettleMinMs)return;
        const bool settled=pulled&&h.still_since&&now>=h.still_since+kHookStillMs;
        if(!settled&&!over){
            if(now>h.cast+kHookCloseWaitMaxMs){Logf("NPC_COMBO hook close node=%d skipped %llums after the cast: %s",h.node,now-h.cast,h.seen?"its move still plays":"its move never started");h={};}
            return;}
        StartHookSlide(h,a,b,hero_moves,0,settled?"settled":"its move over (the hook pulled no one)");
        return;
    }
    // The carry: the goal is `want` from the other one (where it is now) along the line it started on.
    Object* m=h.mover.object;Vector before{},o{};if(!ActorAt(m,before)||!ActorAt(h.other.object,o)){h={};return;}
    const double want=HookWant(h),u=(HookClock()-h.slide_at)/h.length,k=HookProgress(h,u);++h.frames;
    const Vector goal{o.x+h.dir.x*want,o.y+h.dir.y*want,before.z};
    const double lx=goal.x-h.from.x,ly=goal.y-h.from.y,len2=lx*lx+ly*ly;
    const double done=len2>1?((before.x-h.from.x)*lx+(before.y-h.from.y)*ly)/len2:1;  // how far along it is already (the game may carry it too)
    bool ok=true,blocked=false;Call move(m,"K2_SetActorLocation");
    if(k>done+0.002&&len2>1){
        const Vector p{h.from.x+lx*k,h.from.y+ly*k,before.z};  // the mover's own height: gravity keeps it on the ground
        ok=move.Set("NewLocation",p).Set("bSweep",true).Set("bTeleport",false).Invoke();
        Vector after{};ActorAt(m,after);
        const double asked=std::hypot(p.x-before.x,p.y-before.y);double went=std::hypot(after.x-before.x,after.y-before.y);
        // Stopped short on a flat sweep: a step up as a character takes it (a curb, a stair, rising ground: up by
        // kHookStepCm, on, then down onto the floor or back where it was).
        if(ok&&asked>3&&went<asked*0.3){
            const auto sweep=[&](const Vector& to){Call c(m,"K2_SetActorLocation");return c.Set("NewLocation",to).Set("bSweep",true).Set("bTeleport",false).Invoke();};
            Vector up{};sweep({after.x,after.y,after.z+kHookStepCm});ActorAt(m,up);
            Vector on{};sweep({p.x,p.y,up.z});ActorAt(m,on);
            sweep({on.x,on.y,before.z-10});Vector down{};ActorAt(m,down);
            const double stepped=std::hypot(down.x-before.x,down.y-before.y);
            if(stepped>went+1){went=stepped;if(!h.stepped++)Logf("NPC_COMBO hook close node=%d steps up %.0fcm on the way",h.node,down.z-before.z);}
        }
        blocked=asked>3&&went<asked*0.3;
    }
    if(!blocked)h.blocked_since=0;else if(!h.blocked_since)h.blocked_since=now;
    const bool stuck=blocked&&now>=h.blocked_since+kHookStuckMs;
    if(!ok||stuck||(u>=1&&!blocked)){
        Vector ha{},ta{};ActorAt(h.hero.object,ha);ActorAt(h.target.object,ta);
        const std::string why=stuck?"stopped (blocked: "+SweepHit(m,move)+")":!ok?"stopped (the move call failed)":"done";
        Logf("NPC_COMBO hook close node=%d %s after %d frames (%s): %.0fcm apart now",h.node,why.c_str(),h.frames,h.how.c_str(),std::hypot(ta.x-ha.x,ta.y-ha.y));h={};}
}
// ---- 钩子 瞄准 ------------------------------------------------------------------------------------------
// 钩子 goes where the player aims (11-09: "钩子勾到地面后不要返回 钩子允许勾墙壁 用户瞄准的地方释放 距离可以调节"): the lock
// target when it is within the node's reach (hook_dist, from the hero); else the enemy under the crosshair (the view's
// centre line within kHookAimDeg, or kHookAimSideCm of it, in sight, within reach: the nearest to the line); else the
// ground, a wall or a ceiling the crosshair rests on within reach (and at least kHookAnchorMinCm away): an anchor the
// hero flies to whatever the node's mode (DriveHookAnchor); else nothing (the game throws its hook and it comes back).
// The old fallback, the nearest enemy within 60 m, is gone: it took one 39 m away and carried the hero there (11-09).
constexpr double kHookAimDeg=10,kHookAimSideCm=120,kHookAnchorMinCm=250;
struct HookAim { Object* enemy=nullptr;bool anchor=false,locked_far=false;Vector point{},normal{},eye{},dir{};double reach=0,gap=0;std::string how; };
// The camera's place and the direction it looks (the player camera manager's).
bool ViewNow(Object* pawn,Vector& eye,Vector& dir){
    Call manager(ue::Default("/Script/Engine.Default__GameplayStatics"),"GetPlayerCameraManager");
    Object* cam=manager.Set("WorldContextObject",pawn).Set("PlayerIndex",int32_t{0}).Invoke()?manager.Get<Object*>("ReturnValue"):nullptr;
    if(!ue::Alive(cam))return false;
    Call l(cam,"GetCameraLocation"),r(cam,"GetCameraRotation");if(!l.Invoke()||!r.Invoke())return false;
    eye=l.Get<Vector>("ReturnValue");const RotatorD view=r.Get<RotatorD>("ReturnValue");constexpr double kRad=3.14159265358979323846/180.0;
    dir={std::cos(view.pitch*kRad)*std::cos(view.yaw*kRad),std::cos(view.pitch*kRad)*std::sin(view.yaw*kRad),std::sin(view.pitch*kRad)};
    return true;
}
double Dot3(const Vector& a,const Vector& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vector Sub3(const Vector& a,const Vector& b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
HookAim PickHookAim(Object* hero,double reach){
    HookAim aim;aim.reach=reach;Vector at{};if(!ActorAt(hero,at)){aim.how="no hero place";return aim;}
    const char* via="lock";Object* locked=LockedEnemy(hero,via);
    if(locked){Vector e{};const double d=ActorAt(locked,e)?Gap3(at,e):1e9;
        if(d<=reach){aim.enemy=locked;aim.gap=d;aim.how="the lock target";return aim;}
        aim.locked_far=true;aim.how="the lock target "+std::to_string(static_cast<int>(d/100))+" m away is out of reach; ";}
    Vector eye{},dir{};if(!ViewNow(hero,eye,dir)){aim.how+="no camera";return aim;}
    aim.eye=eye;aim.dir=dir;const double hero_along=Dot3(Sub3(at,eye),dir);
    // An enemy under the crosshair.
    {Call actors(ue::Default("/Script/Engine.Default__GameplayStatics"),"GetAllActorsOfClass");
     if(actors.Set("WorldContextObject",hero).Set("ActorClass",Class("/Script/Engine.Pawn")).Invoke()){
        const Array list=actors.Get<Array>("OutActors");double best=1e9;Object* found=nullptr;double found_deg=0;
        if(list.data&&list.num>0&&list.num<=2048&&FaceReadable(list.data,static_cast<size_t>(list.num)*sizeof(Object*)))
            for(int32_t i=0;i<list.num;++i){Object* pawn=static_cast<Object**>(list.data)[i];Vector e{};
                if(!ue::Alive(pawn)||pawn==hero||!ActorAt(pawn,e)||Gap3(at,e)>reach||!EnemyOf(pawn,hero))continue;
                const Vector v=Sub3(e,eye);const double along=Dot3(v,dir);if(along<=hero_along-50)continue;
                const Vector side_v{v.x-dir.x*along,v.y-dir.y*along,v.z-dir.z*along};const double side=std::sqrt(Dot3(side_v,side_v));
                const double deg=std::atan2(side,along)*180/3.14159265358979323846;
                if(deg>kHookAimDeg&&side>kHookAimSideCm)continue;
                std::vector<Object*> skip=TraceIgnore(hero,pawn);if(TraceLine(hero,{at.x,at.y,at.z+60},e,skip)==1)continue;  // out of the hero's sight
                if(side<best){best=side;found=pawn;found_deg=deg;}}
        ue::Free(list.data);
        if(found){Vector e{};ActorAt(found,e);aim.enemy=found;aim.gap=Gap3(at,e);
            char t[96]{};_snprintf_s(t,sizeof(t),_TRUNCATE,"the enemy under the crosshair (%.1f deg off, %.0f cm across)",found_deg,best);aim.how+=t;return aim;}}}
    // The ground, a wall or a ceiling under the crosshair.
    std::vector<Object*> skip=TraceIgnore(hero,nullptr);
    const double length=(std::max)(0.0,hero_along)+reach+50;const Vector to{eye.x+dir.x*length,eye.y+dir.y*length,eye.z+dir.z*length};
    Vector point{},normal{};int hit=TraceLine(hero,eye,to,skip,&point,&normal);
    // A camera pressed against a wall behind the hero sees that wall first: the ray is traced again from the hero on.
    if(hit==1&&Dot3(Sub3(point,eye),dir)<=hero_along+30){const double from_along=(std::max)(0.0,hero_along)+30;
        hit=TraceLine(hero,{eye.x+dir.x*from_along,eye.y+dir.y*from_along,eye.z+dir.z*from_along},to,skip,&point,&normal);if(hit==1)aim.how+="(past the wall behind the camera) ";}
    if(hit==1){const double d=Gap3(at,point),along=Dot3(Sub3(point,eye),dir);
        if(along<=hero_along+30)aim.how+="the crosshair rests on something between the camera and the hero";
        else if(d>reach)aim.how+="the crosshair rests "+std::to_string(static_cast<int>(d/100))+" m away, out of reach";
        else if(d<kHookAnchorMinCm)aim.how+="the crosshair rests too near ("+std::to_string(static_cast<int>(d))+" cm)";
        else{aim.anchor=true;aim.point=point;aim.normal=normal;aim.gap=d;
            aim.how+=normal.z>=0.7?"the ground under the crosshair":normal.z<=-0.7?"a ceiling under the crosshair":"a wall under the crosshair";}}
    else aim.how+=hit==0?"nothing under the crosshair within reach":"the aim trace failed";
    return aim;
}
void AimHook(Object* hero,int index,bool self,const HookAim& aim){
    Object* target=aim.enemy;const char* via=aim.how.c_str();const char* how="none";
    if(target){const char* lv="lock";Object* locked=LockedEnemy(hero,lv);how=locked==target?"kept":LockOn(hero,target);}
    Vector a{},b{};
    const double d=target&&ActorAt(hero,a)&&ActorAt(target,b)?std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)):-1;
    Logf("NPC_COMBO hook node=%d %s target=%s via=%s lock=%s distance=%.0fcm reach=%.0fcm",index,self?"go (HookMove)":"pull (Drag)",Text(target).c_str(),via,how,d,aim.reach);
    hook_probe_={};hook_close_={};
    if(target){hook_probe_.hero=Id(hero);hook_probe_.target=Id(target);hook_probe_.hero_at=a;hook_probe_.target_at=b;hook_probe_.at=GetTickCount64();hook_probe_.node=index;
        hook_close_.hero=hook_probe_.hero;hook_close_.target=hook_probe_.target;hook_close_.hero_at=hook_close_.hero_last=a;hook_close_.target_at=hook_close_.target_last=b;
        hook_close_.cast=hook_probe_.at;hook_close_.node=index;hook_close_.self=self;hook_close_.phase=1;hook_close_.start_gap=std::hypot(b.x-a.x,b.y-a.y);
        if(const NpcCombo::Node* n=ComboNode(index))hook_close_.ability=n->ability;}
}
// ---- 钩子 锚点 ------------------------------------------------------------------------------------------
// An anchor (the ground, a wall, a ceiling the hook was aimed at): the hero flies there and the hook does not come back.
// The game's HookMove (勾过去) is cast toward it: the hero is turned to face it and the move's own warp target
// (HookMoveTarget, the MotionWarping its timelines follow) is held on it. The move's Lego timelines are Main (the
// throw), then Jump / Loop / End (flying to a hooked target) or Failed_L / Failed_R (the chain coming back, what an
// aim at the ground did, 11-09). Once its chain reaches the anchor (Main at kHookConnectS of its own time) or it starts
// to come back, the hook is taken over: the move's Loop timeline plays in place of what it was doing (its flying pose),
// and the move's own flight (its Blueprint moves the hero toward its TargetLoc every tick while Loop plays, then plays
// End on arrival) is pointed at the anchor's spot (its capsule standing on the ground there, off the wall but not
// below the floor in front of it, or under the ceiling): TargetLoc, StartLoc, SourceToTargetVector and LengthSqr are
// written at the take-over and TargetLoc every frame after (11-09: left alone, it flew on 6.5 m past a wall spot to its
// own aim trace's end). The 钩子速度 is the move's own pace (the hero's time, as a 加速). A flight that does not close
// in within kHookGameFlyS is flown here instead: a straight line at kHookFlyCmS at the top (times the 钩子速度) with an
// ease in and out, as a flying character (no gravity) swept every frame, then End. kHookEndHoldS after the landing
// gravity comes back (a wall or ceiling spot: the hero drops from there). While End plays the hero stays over the
// spot: its movement is stopped at the landing, its root motion moves nothing (AnimRootMotionTranslationScale held at
// 0 until the move is over, then 1 again) and its place across is held where it landed (its height is free): End's
// lunge and warp are made for an enemy that stops the hero, and with none the hero went on 9.5 m, then 5.9 m with the
// flight's speed left (11-09). If the game
// itself goes on to Jump / Loop before the take-over (it hooked something on its own) it is left to the game. With no
// enemy and no spot in reach the hook is a miss: the pull (Drag) is thrown and comes back (HookMove alone flies to the
// end of its own aim trace, 16 m in one test, past any reach).
constexpr double kHookConnectS=0.44,kHookFlyCmS=2600,kHookLandCm=60,kHookEndHoldS=0.42,kHookAnchorStuckS=0.12,kHookGameFlyS=0.3,kHookAnchorMaxS=4;
constexpr ULONGLONG kHookAnchorWaitMs=2500;
// phase: 1 the throw, 2 the move's own flight, 3 flown here, 4 landing.
struct HookAnchor { Identity hero;int node=-1,phase=0,frames=0;Vector goal{},point{},normal{},from{},landed{};double length=0,fly_at=0,land_at=0,pace=1,dist=0,blocked_at=0,taken_at=0;
                    ULONGLONG cast=0;std::string how,took;bool seen=false,ended_seen=false,pinned=false,fell=false; };
HookAnchor hook_anchor_;
bool HookAnchorBusy(){return hook_anchor_.phase>=2;}
// The hero's motion warp target `name` at `at` (MotionWarpingComponent.AddOrUpdateWarpTargetFromLocation).
bool SetWarpTarget(Object* pawn,const wchar_t* name,const Vector& at){
    static Object* const warp_class=Class("/Script/MotionWarping.MotionWarpingComponent");if(!warp_class)return false;
    Call get(pawn,"GetComponentByClass");Object* warp=get.Set("ComponentClass",warp_class).Invoke()?get.Get<Object*>("ReturnValue"):nullptr;if(!ue::Alive(warp))return false;
    const Name n=ue::MakeName(name);if(n.None())return false;
    return Call(warp,"AddOrUpdateWarpTargetFromLocation").Set("WarpTargetName",n).Set("TargetLocation",at).Invoke();
}
bool SetMoveMode(Object* pawn,uint8_t mode){Object* move=Property<Object*>(pawn,"CharacterMovement");
    return ue::Alive(move)&&Call(move,"SetMovementMode").Set("NewMovementMode",mode).Set("NewCustomMode",uint8_t{0}).Invoke();}
// LegoAbility.PlayTimelineByName / EndTimelineByName on `ability` with timeline `name`: its name parameter (the first
// 8-byte one named ...Name) set, a rate or scale at 1, the rest left zero. Its parameters go to the log once.
bool CallTimeline(Object* ability,const char* function,const wchar_t* name){
    Object* f=ue::Alive(ability)?ue::FindFunction(ue::ClassOf(ability),function):nullptr;if(!f)return false;
    ue::Field a[12];const size_t n=ue::Fields(f,a,std::size(a));Call call(ability,f);bool named=false;std::string params;
    for(size_t i=0;i<n;++i){const std::string lower=Lower(a[i].name);params+=std::string(params.empty()?"":",")+a[i].name+":"+std::to_string(a[i].size);
        if(lower=="returnvalue")continue;
        if(!named&&a[i].size==8&&lower.find("name")!=std::string::npos){call.Set(a[i].name,ue::MakeName(name));named=true;}
        else if(a[i].size==4&&(lower.find("rate")!=std::string::npos||lower.find("scale")!=std::string::npos))call.Set(a[i].name,1.0f);}
    static std::vector<std::string> told;
    if(std::find(told.begin(),told.end(),std::string(function))==told.end()){told.push_back(function);Logf("NPC_COMBO hook anchor: %s(%s)",function,params.c_str());}
    return named&&call.Invoke();
}
// The running hook move's timelines now (their names, with the Main one's time).
std::vector<std::string> HookTimelines(Object* ability,double& main_time){
    std::vector<std::string> out;main_time=-1;
    static Object* const task_class=Class("/Script/LegoAbility.LGATask_PlayTimeline");
    const int32_t time_at=task_class?ue::Offset(task_class,"CurrentTime"):-1,line_at=task_class?ue::Offset(task_class,"Timeline"):-1;
    const int32_t tasks_at=ue::Alive(ability)?Offset(ability,"ActiveTasks"):-1;
    if(!task_class||time_at<0||line_at<0||tasks_at<0||!FaceReadable(ability+tasks_at,sizeof(Array)))return out;
    const Array tasks=ue::At<Array>(ability,tasks_at);
    if(tasks.num<=0||tasks.num>64||!FaceReadable(tasks.data,static_cast<size_t>(tasks.num)*sizeof(Object*)))return out;
    for(int32_t i=0;i<tasks.num;++i){Object* t=static_cast<Object**>(tasks.data)[i];if(!ue::Alive(t)||!ue::IsA(t,task_class))continue;
        const std::string line=Text(ue::At<Object*>(t,line_at));out.push_back(line);if(line=="Main")main_time=ue::At<float>(t,time_at);}
    return out;
}
// Where the hero's capsule ends: standing on the ground spot, off a wall by its radius, under a ceiling.
Vector AnchorGoal(Object* hero,const Vector& point,const Vector& normal){
    double half=90,radius=34;CapsuleOf(hero,half,radius);
    if(normal.z>=0.7)return {point.x,point.y,point.z+half+2};
    if(normal.z<=-0.7)return {point.x,point.y,point.z-half-12};
    const double flat=std::hypot(normal.x,normal.y);const double nx=flat>0.01?normal.x/flat:0,ny=flat>0.01?normal.y/flat:0;
    Vector goal{point.x+nx*(radius+12),point.y+ny*(radius+12),point.z};
    std::vector<Object*> skip=TraceIgnore(hero,nullptr);Vector floor{},up{};
    if(TraceLine(hero,{goal.x,goal.y,goal.z+half},{goal.x,goal.y,goal.z-half-60},skip,&floor,&up)==1&&up.z>=0.7)goal.z=(std::max)(goal.z,floor.z+half+2);
    return goal;
}
// The move's own flight pointed at the anchor: its Blueprint's TargetLoc (and at the take-over StartLoc,
// SourceToTargetVector, LengthSqr) as it would have set them for a target there. How many were written.
int AimHookFlight(Object* ability,Object* hero,const Vector& goal,bool all){
    if(!ue::Alive(ability))return 0;int written=0;
    const auto put=[&](const char* name,const void* value,size_t size){const int32_t at=Offset(ability,name);
        if(at>=0&&FaceReadable(ability+at,size)){std::memcpy(ability+at,value,size);++written;}};
    put("TargetLoc",&goal,sizeof(Vector));
    if(all){Vector at{};ActorAt(hero,at);const Vector d=Sub3(goal,at);const double sq=Dot3(d,d);
        put("StartLoc",&at,sizeof(Vector));put("PreLocation",&at,sizeof(Vector));put("SourceToTargetVector",&d,sizeof(Vector));put("LengthSqr",&sq,sizeof(double));}
    return written;
}
void StartHookAnchor(Object* hero,int index,const HookAim& aim){
    hook_anchor_={};HookAnchor& h=hook_anchor_;
    h.hero=Id(hero);h.node=index;h.phase=1;h.point=aim.point;h.normal=aim.normal;h.goal=AnchorGoal(hero,aim.point,aim.normal);h.cast=GetTickCount64();
    h.pace=std::clamp(NpcCombo::HookPace(combo_.config,index),0.5,3.0);h.how=aim.how;
    Vector at{};ActorAt(hero,at);
    const double yaw=std::atan2(h.goal.y-at.y,h.goal.x-at.x)*180/3.14159265358979323846;
    const bool turned=Call(hero,"K2_SetActorRotation").Set("NewRotation",RotatorD{0,yaw,0}).Set("bTeleportPhysics",false).Invoke();
    const bool warp=SetWarpTarget(hero,L"HookMoveTarget",h.goal);
    Logf("NPC_COMBO hook node=%d anchor: %s at (%.0f,%.0f,%.0f) normal (%.2f,%.2f,%.2f), %.0f cm away; the hero ends at (%.0f,%.0f,%.0f); turned=%d warp=%d speed x%.2f",
         index,aim.how.c_str(),aim.point.x,aim.point.y,aim.point.z,aim.normal.x,aim.normal.y,aim.normal.z,aim.gap,h.goal.x,h.goal.y,h.goal.z,turned?1:0,warp?1:0,h.pace);
}
bool RootMotionScale(Object* pawn,float scale){return Call(pawn,"SetAnimRootMotionTranslationScale").Set("InAnimRootMotionTranslationScale",scale).Invoke();}
bool StopHeroMovement(Object* pawn){Object* move=Property<Object*>(pawn,"CharacterMovement");return ue::Alive(move)&&Call(move,"StopMovementImmediately").Invoke();}
// Landing: movement stopped, the place across kept from here.
void LandHookAnchor(HookAnchor& h,Object* hero,double clock){StopHeroMovement(hero);ActorAt(hero,h.landed);h.phase=4;h.land_at=clock;}
void EndHookAnchor(const char* why){
    HookAnchor& h=hook_anchor_;if(!h.phase)return;
    if(h.phase>=2&&!h.fell&&Same(h.hero))SetMoveMode(h.hero.object,3);  // falling: gravity is back
    if(h.pinned&&Same(h.hero))RootMotionScale(h.hero.object,1.f);
    Vector at{};if(Same(h.hero))ActorAt(h.hero.object,at);
    Logf("NPC_COMBO hook node=%d anchor %s after %d frames (%s): %.0f cm from where it ends, %llums after the cast",h.node,why,h.frames,h.took.empty()?"-":h.took.c_str(),
         Gap3(at,h.goal),GetTickCount64()-h.cast);
    h={};
}
void DriveHookAnchor(){
    HookAnchor& h=hook_anchor_;if(!h.phase)return;
    if(!Same(h.hero)){h={};return;}
    Object* hero=h.hero.object;const ULONGLONG now=GetTickCount64();const double clock=ClockSeconds();
    if(combo_.playing!=h.node){EndHookAnchor("cut: another node plays");return;}
    SetWarpTarget(hero,L"HookMoveTarget",h.goal);
    Object* ability=nullptr;
    if(skill_.handle>0&&skill_.name==NpcCombo::kHookGo&&Same(skill_.asc)){bool instance=false;ability=SpecAbility(skill_.asc.object,skill_.handle,instance);if(!instance)ability=nullptr;}
    double main_time=-1;const std::vector<std::string> lines=HookTimelines(ability,main_time);if(ability)h.seen=true;
    const auto has=[&](const char* n){return std::find(lines.begin(),lines.end(),std::string(n))!=lines.end();};
    if(h.phase==1){
        if(has("Jump")||has("Loop")||has("End")||has("TooCloseEnd")){Logf("NPC_COMBO hook node=%d anchor left to the game: it hooked something itself",h.node);h={};return;}
        const bool failed=has("Failed_L")||has("Failed_R"),connected=main_time>=kHookConnectS,over=h.seen&&!ability;
        if(!failed&&!connected&&!over){if(now>h.cast+kHookAnchorWaitMs)EndHookAnchor("given up: the hook move never came");return;}
        // Taken over: what the move does now gives way to its Loop (its flying pose), its flight pointed at the spot.
        std::string took;int fields=0;
        if(ability){for(const char* n:{"Failed_L","Failed_R","Main"})if(has(n)){const std::wstring w(n,n+std::strlen(n));if(CallTimeline(ability,"EndTimelineByName",w.c_str()))took+=std::string("end ")+n+" ";}
                    fields=AimHookFlight(ability,hero,h.goal,true);
                    took+=CallTimeline(ability,"PlayTimelineByName",L"Loop")?"play Loop":"Loop refused";}
        else took="no move left";
        h.took=took;SetMoveMode(hero,5);  // flying: no gravity on the way
        ActorAt(hero,h.from);h.dist=Gap3(h.from,h.goal);h.taken_at=clock;h.frames=0;
        h.phase=ability?2:3;
        if(h.phase==3){h.length=(std::max)(0.12,1.5*h.dist/(kHookFlyCmS*h.pace));h.fly_at=clock;}
        Logf("NPC_COMBO hook node=%d anchor: taken over %llums after the cast (%s; Main at %.2fs): %s, %d flight fields set; %.0f cm to fly (%s)",h.node,now-h.cast,
             failed?"the chain was coming back":connected?"the chain reached it":"the move was over",main_time,took.c_str(),fields,h.dist,h.phase==2?"the move's own flight":"flown here");
        return;
    }
    ++h.frames;Vector at{};ActorAt(hero,at);const double left=Gap3(at,h.goal);
    if(clock>h.taken_at+kHookAnchorMaxS){EndHookAnchor("given up: too long");return;}
    if(h.phase==2){  // the move's own flight
        if(ability)AimHookFlight(ability,hero,h.goal,false);
        if(has("End")||has("TooCloseEnd")){LandHookAnchor(h,hero,clock);h.took+=", the move lands itself";
            Logf("NPC_COMBO hook node=%d anchor: the move lands itself after %d frames, %.0f cm from the spot",h.node,h.frames,left);return;}
        if(!ability){LandHookAnchor(h,hero,clock);h.took+=", the move ended";Logf("NPC_COMBO hook node=%d anchor: the move ended in flight, %.0f cm from the spot",h.node,left);return;}
        if(left<=kHookLandCm){CallTimeline(ability,"EndTimelineByName",L"Loop");h.took+=CallTimeline(ability,"PlayTimelineByName",L"End")?", land End":", End refused";
            LandHookAnchor(h,hero,clock);Logf("NPC_COMBO hook node=%d anchor: at the spot after %d frames: End",h.node,h.frames);return;}
        if(clock>=h.taken_at+kHookGameFlyS&&left>h.dist*0.8){  // the move does not fly it: flown here
            ActorAt(hero,h.from);h.dist=left;h.length=(std::max)(0.12,1.5*h.dist/(kHookFlyCmS*h.pace));h.fly_at=clock;h.phase=3;h.took+=", flown here";
            Logf("NPC_COMBO hook node=%d anchor: the move's flight did not close in (%.0f cm left after %.2fs): flown here over %.2fs",h.node,left,clock-h.taken_at,h.length);}
        return;
    }
    if(h.phase==3){  // flown here
        if(ability)AimHookFlight(ability,hero,h.goal,false);
        const double u=std::clamp((clock-h.fly_at)/h.length,0.0,1.0),k=3*u*u-2*u*u*u;
        const Vector want{h.from.x+(h.goal.x-h.from.x)*k,h.from.y+(h.goal.y-h.from.y)*k,h.from.z+(h.goal.z-h.from.z)*k};
        Call move(hero,"K2_SetActorLocation");const bool ok=move.Set("NewLocation",want).Set("bSweep",true).Set("bTeleport",false).Invoke();
        Vector after{};ActorAt(hero,after);
        const double asked=Gap3(want,at),went=Gap3(after,at);
        if(ok&&asked>3&&went<asked*0.3){if(h.blocked_at<=0)h.blocked_at=clock;}else h.blocked_at=0;
        const bool stuck=h.blocked_at>0&&clock>=h.blocked_at+kHookAnchorStuckS;
        if(!ok||stuck||u>=1||Gap3(after,h.goal)<=kHookLandCm){
            std::string land;
            if(ability){CallTimeline(ability,"EndTimelineByName",L"Loop");land=CallTimeline(ability,"PlayTimelineByName",L"End")?"End":"End refused";}else land="no move";
            h.took+=", land "+land+(stuck?" (stopped: "+SweepHit(hero,move)+")":"");LandHookAnchor(h,hero,clock);
            Logf("NPC_COMBO hook node=%d anchor: landing after %d frames, %.0f cm from the spot%s",h.node,h.frames,Gap3(after,h.goal),stuck?" (blocked on the way)":"");}
        return;
    }
    // Landing: End plays in place; gravity back after kHookEndHoldS; over with the move (or its End).
    if(ability)AimHookFlight(ability,hero,h.goal,false);
    if(RootMotionScale(hero,0.f))h.pinned=true;
    if(std::hypot(at.x-h.landed.x,at.y-h.landed.y)>1){Call hold(hero,"K2_SetActorLocation");hold.Set("NewLocation",Vector{h.landed.x,h.landed.y,at.z}).Set("bSweep",true).Set("bTeleport",false).Invoke();}
    if(!h.fell&&clock>=h.land_at+kHookEndHoldS/h.pace){SetMoveMode(hero,3);h.fell=true;}
    if(h.fell&&(!ability||!(has("End")||has("TooCloseEnd")||has("Loop"))))EndHookAnchor(ability?"done (its End is over)":"done (the move is over)");
}
// ---- 滞空 ----------------------------------------------------------------------------------------
// 滞空 / 空中释放 (NpcCombo::AirFor) and 强制受击 击飞's height (NpcCombo::ForceLaunchFor) on a character: the hero, or an
// enemy a move of the chain hits. A lift of h cm is a throw up as a jump of that height: the character goes falling
// (MOVE_Falling) at v0 = sqrt(2 g h) upward, g its own gravity (CharacterMovement.GetGravityZ), and rises to its top
// v0 / g later, slowed by that gravity; while it rises its upward speed is held at least at v0 - g t (a hit reaction's
// or a montage's root motion that zeroes it, or a walking mode put back, would end the throw). A hang of k percent
// then (or at once, when it is in the air already) takes k percent of its gravity away (CharacterMovement.GravityScale,
// given back when it ends) and as much of the speed it falls at: 100 hovers. The hero's hang lasts while its move plays,
// an enemy's while the move that hit it plays and kAirFoeHoldS after its last hit; landing ends it. Only a walking or
// falling character is thrown (one flying, swimming or scripted is left alone), and a hang ends when the character
// lands or another movement mode takes over. That is the hero's. An enemy's arc is all ours instead: the game turns
// an enemy off the ground to flying and back (11-09, the test dummy: MOVE_Falling and MOVE_Flying in turn every ~6
// frames; left flying 11 cm up once a throw was dropped), so its vertical speed vz is kept here over time (v0 - g t on
// the way up, the hang's share of g while held, all of g after, its own gravity scale at 0 meanwhile) and carried in
// whichever mode it is in: falling, as its vertical speed; flying, its own vertical speed zeroed. Its height is kept
// on the arc too (want_z: the throw's own height on the way up, vz integrated after): a hit reaction moves it as well
// (11-09: a 300 cm throw topped out at 253 cm, a 500 cm 击飞 at 362 cm), so every frame it is moved back onto the arc
// (swept: a floor under it on the way down lands it, a ceiling over it on the way up ends the rise). It is carried
// down to the floor after the hold too, then walks (MOVE_Walking). kAirFlownMaxS after its lift or its hold,
// whichever is later, it is left falling. The hero's rise is kept to the throw's own height in the same way (only
// down: 11-09, a 300 cm throw under a move topped out at 371 cm, 80 cm at 110 cm); its hold is left to the move.
constexpr double kAirFoeHoldS=0.8,kAirRiseMaxS=1.6,kAirFlownMaxS=6,kAirFallMaxCmS=4000;
struct AirBody { Identity actor,move;std::string who;float gravity=1;double g=980,v0=0,rise_at=0,rise_s=0,hang=0,until=0,start_z=0,top_z=0,lifted_at=0;
                 double last_clock=0,last_vz=0,vz=0,want_z=0,kept_cm=0;int phase=0,node=-1,probe=0,kept=0;bool hero=false,hanging=false,flown=false;ULONGLONG started=0;ComboPlayback::LiftOnce lift_once; };
// phase 1 rising, 2 held (an enemy: then falling); vz an enemy's vertical speed (ours), want_z its height on the arc;
// kept / kept_cm: the frames it was moved back onto the arc and by how much in all; flown: the game has flown it;
// probe: tests, frames logged (npcretarget airfoe / airlift; airprobe <frames> for the chain's lifts)
std::vector<AirBody> air_bodies_;
volatile LONG air_test_=0;double air_test_cm_=0,air_test_hang_=0,air_test_s_=1;  // tests: npcretarget airlift / airfoe / jump
volatile LONG air_probe_frames_=0;  // tests: npcretarget airprobe <frames>, the chain's lifts logged frame by frame
Object* MoveOf(Object* actor){Object* m=ue::Alive(actor)?Property<Object*>(actor,"CharacterMovement"):nullptr;return ue::Alive(m)?m:nullptr;}
uint8_t ModeOf(Object* move){return move&&Offset(move,"MovementMode")>=0?Property<uint8_t>(move,"MovementMode"):0;}
bool Airborne(Object* actor){return ModeOf(MoveOf(actor))==3;}
double ActorZ(Object* actor){Vector at{};return ActorAt(actor,at)?at.z:0;}
// Standing on a floor: one within 15 cm under the capsule.
bool OnFloor(Object* actor){
    double half=0,radius=0;Vector at{};if(!CapsuleOf(actor,half,radius)||!ActorAt(actor,at))return false;
    std::vector<Object*> skip=TraceIgnore(actor,nullptr);Vector point{},normal{};
    return TraceLine(actor,at,{at.x,at.y,at.z-half-15},skip,&point,&normal)==1&&normal.z>=0.7;
}
void EndAir(const AirBody& b,const char* why){
    if(Same(b.move))Put(b.move.object,"GravityScale",b.gravity);
    char kept[96]{};if(b.kept)_snprintf_s(kept,sizeof(kept),_TRUNCATE,"; moved back onto the arc on %d frames, %.0f cm in all",b.kept,b.kept_cm);
    Logf("NPC_AIR %s done (%s) %.2fs after the lift: top %.0f cm over the start, now %+.0f cm%s",b.who.c_str(),why,ClockSeconds()-b.lifted_at,b.top_z-b.start_z,
         Same(b.actor)?ActorZ(b.actor.object)-b.start_z:0.0,kept);
}
// `actor` moved to height z where it is (swept); how far it went.
double AirShift(Object* actor,const Vector& at,double z){
    Call shift(actor,"K2_SetActorLocation");shift.Set("NewLocation",Vector{at.x,at.y,z}).Set("bSweep",true).Set("bTeleport",false).Invoke();
    Vector after{};return ActorAt(actor,after)?after.z-at.z:0.0;
}
// `actor` thrown up `cm` (0: none) and held at `hang` percent until `until` (ClockSeconds; the hero's: while move
// `node` started at `started` plays). False when it is not one to lift (or on the ground with no lift).
bool AirLift(Object* actor,double cm,double hang,double until,bool hero,int node,ULONGLONG started,const char* who){
    Object* move=MoveOf(actor);if(!move)return false;
    const uint8_t mode=ModeOf(move);
    if(mode!=1&&mode!=2&&mode!=3&&!(mode==5&&!hero))return false;  // walking, navmesh walking, falling (an enemy: flying too)
    auto it=std::find_if(air_bodies_.begin(),air_bodies_.end(),[&](const AirBody& b){return b.actor.object==actor&&Same(b.actor);});
    const double clock=ClockSeconds();
    if(it==air_bodies_.end()){
        if(cm<=0.5&&mode!=3&&mode!=5)return false;  // on the ground, no lift: nothing to hold
        AirBody b;b.actor=Id(actor);b.move=Id(move);b.who=who?who:Text(actor);
        b.gravity=Property<float>(move,"GravityScale");if(!(b.gravity>0.01f&&b.gravity<20.f))b.gravity=1.f;
        Call g(move,"GetGravityZ");const double gz=g.Invoke()?std::fabs(static_cast<double>(g.Get<float>("ReturnValue"))):0.0;b.g=gz>1?gz:980.0*b.gravity;
        b.start_z=b.top_z=b.want_z=ActorZ(actor);b.lifted_at=clock;b.probe=static_cast<int>(InterlockedCompareExchange(&air_probe_frames_,0,0));
        air_bodies_.push_back(b);it=air_bodies_.end()-1;}
    AirBody& b=*it;b.hero=hero;b.node=node;b.started=started;b.hang=std::clamp(hang/100.0,0.0,1.0);b.until=(std::max)(b.until,until);
    if(b.lift_once.Take(started,node,hero,cm)){
        b.v0=std::sqrt(2*b.g*cm);b.rise_at=clock;b.rise_s=b.v0/b.g;b.phase=1;b.hanging=false;b.start_z=b.top_z=b.want_z=ActorZ(actor);b.lifted_at=clock;b.vz=b.v0;
        Put(move,"GravityScale",hero?b.gravity:0.f);  // an enemy's gravity is ours (DriveAir)
        if(mode!=3&&mode!=5)SetMoveMode(actor,3);
        Vector v=Property<Vector>(move,"Velocity");v.z=b.v0;Put(move,"Velocity",v);
        Logf("NPC_AIR %s thrown up %.0f cm: %.0f cm/s for %.2fs (gravity %.0f cm/s2, scale %.2f), then held %.0f%% cast=%llu node=%d",b.who.c_str(),cm,b.v0,b.rise_s,b.g,b.gravity,b.hang*100,started,node);
    }else if(b.phase!=1){
        if(b.phase!=2&&!hero){b.vz=Property<Vector>(move,"Velocity").z;b.want_z=ActorZ(actor);Put(move,"GravityScale",0.f);}  // held from where it is
        b.phase=2;}
    return true;
}
void AirOnHit(Object* target,int cm,int hang,ULONGLONG started){
    AirLift(target,cm,hang,hang>0?ClockSeconds()+kAirFoeHoldS:0.0,false,-1,started,nullptr);
}
void AirKeepFoes(ULONGLONG started){const double clock=ClockSeconds();for(AirBody& b:air_bodies_)if(!b.hero&&b.started==started)b.until=(std::max)(b.until,clock+0.05);}
// A combo move (now running) under a 滞空, or cast in the air under 空中释放: the hero thrown up and held, the enemies
// it hits likewise (ScaleSkillDamage).
void StartComboAir(){
    const int node=combo_.playing;
    const NpcCombo::AirSet air=NpcCombo::AirFor(combo_.config,combo_.trigger,node,air_cast_node_==node&&air_cast_airborne_);
    if(!air.on)return;
    if(air.foe_lift>0||air.foe_hang>0){combo_air_started_=combo_.started;combo_air_lift_=air.foe_lift;combo_air_hang_=air.foe_hang;}
    const bool self=(air.self_lift>0||air.self_hang>0)&&Same(ChainPawn())&&AirLift(ChainPawn().object,air.self_lift,air.self_hang,0.0,true,node,combo_.started,"the hero");
    Logf("NPC_COMBO air node=%d move=%s (%s): hero +%dcm held %d%%%s; enemies hit +%dcm held %d%%",node,PlayingName().c_str(),air.fixed?"air casts: cast in the air":"its 滞空 node",
         air.self_lift,air.self_hang,self||(!air.self_lift&&!air.self_hang)?"":" (not lifted: on the ground with no lift, or not walking / falling)",air.foe_lift,air.foe_hang);
}
void DriveAir(){
    if(const LONG test=InterlockedExchange(&air_test_,0)){
        Context c;
        if(ContextNow(c)){
            if(test==3){const bool ok=Call(c.pawn,"Jump").Invoke();Logf("NPC_TEST jump ok=%d mode=%u",ok?1:0,static_cast<unsigned>(ModeOf(MoveOf(c.pawn))));}
            else{
                Object* target=c.pawn;
                if(test==2){std::vector<DamageTarget> around;ScanTargets(c.pawn,c.pawn,around);Vector at{};ActorAt(c.pawn,at);double best=1e18;target=nullptr;
                    for(const DamageTarget& t:around){Vector p{};if(Same(t.pawn)&&ActorAt(t.pawn.object,p)){const double d=Gap3(at,p);if(d<best){best=d;target=t.pawn.object;}}}}
                const bool ok=target&&AirLift(target,air_test_cm_,air_test_hang_,ClockSeconds()+air_test_s_,test==1,-1,0,test==1?"the hero (test)":"the nearest enemy (test)");
                if(ok)for(AirBody& b:air_bodies_)if(b.actor.object==target)b.probe=40;
                Logf("NPC_TEST air%s %.0fcm hang %.0f%% %.1fs: %s",test==1?"lift":"foe",air_test_cm_,air_test_hang_,air_test_s_,ok?"lifted":target?"refused (not walking / falling)":"no enemy");}
        }
    }
    if(air_bodies_.empty())return;
    const double clock=ClockSeconds();
    for(size_t i=0;i<air_bodies_.size();){
        AirBody& b=air_bodies_[i];
        if(!Same(b.actor)||!Same(b.move)){air_bodies_.erase(air_bodies_.begin()+static_cast<std::ptrdiff_t>(i));continue;}
        const auto drop=[&](const char* why){EndAir(b,why);air_bodies_.erase(air_bodies_.begin()+static_cast<std::ptrdiff_t>(i));};
        Object* actor=b.actor.object,*move=b.move.object;const uint8_t mode=ModeOf(move);
        const double top_before=b.top_z;
        b.top_z=(std::max)(b.top_z,ActorZ(actor));
        if(b.probe>0){--b.probe;const Vector v=Property<Vector>(move,"Velocity");double half=0,radius=0;CapsuleOf(actor,half,radius);
            Logf("NPC_AIR probe %s t=%.3f phase=%d mode=%u custom=%u z=%+.0f arc=%+.0f vz=%.0f ours=%.0f gravityScale=%.2f half=%.0f kept=%d/%.0fcm",b.who.c_str(),clock-b.lifted_at,b.phase,
                 static_cast<unsigned>(mode),static_cast<unsigned>(Property<uint8_t>(move,"CustomMovementMode")),ActorZ(actor)-b.start_z,b.want_z-b.start_z,v.z,b.vz,
                 static_cast<double>(Property<float>(move,"GravityScale")),half,b.kept,b.kept_cm);}
        if(b.hero&&b.node>=0&&combo_.phase==ComboRun::Phase::Running&&combo_.playing==b.node&&combo_.started==b.started)b.until=(std::max)(b.until,clock+0.05);
        const double dt=b.last_clock>0?std::clamp(clock-b.last_clock,0.0,0.1):0.0;b.last_clock=clock;
        if(!b.hero){  // an enemy: its arc is ours, in whichever mode the game has it
            if(mode!=1&&mode!=2&&mode!=3&&mode!=5){char why[64]{};_snprintf_s(why,sizeof(why),_TRUNCATE,"movement mode %u took over",static_cast<unsigned>(mode));drop(why);continue;}
            if(b.phase==1){
                const double t=clock-b.rise_at;
                if(t<b.rise_s&&t<kAirRiseMaxS){b.vz=b.v0-b.g*t;b.want_z=b.start_z+b.v0*t-0.5*b.g*t*t;}
                else{const double top=(std::min)(t,b.rise_s);b.phase=2;b.vz=0;b.want_z=b.start_z+b.v0*top-0.5*b.g*top*top;
                     Logf("NPC_AIR %s at the top after %.2fs: %.0f cm over the start (the arc's %.0f)",b.who.c_str(),t,b.top_z-b.start_z,b.want_z-b.start_z);}
            }
            if(b.phase==2){
                const bool held=b.hang>0.001&&clock<=b.until;
                if(held&&!b.hanging){b.hanging=true;if(b.vz<0)b.vz*=1-b.hang;}
                b.vz=(std::max)(b.vz-(held?b.g*(1-b.hang):b.g)*dt,-kAirFallMaxCmS);
                b.want_z+=b.vz*dt;
                if(b.vz<0&&mode!=3&&OnFloor(actor)){if(mode==5)SetMoveMode(actor,1);drop(held?"landed while held":"landed");continue;}
            }
            if(clock-(std::max)(b.lifted_at,b.until)>kAirFlownMaxS){SetMoveMode(actor,3);drop("too long in the air: left falling");continue;}
            Put(move,"GravityScale",0.f);
            if(mode==5){  // flying: its own vertical speed zeroed, its height carried by the move back onto the arc below
                if(!b.flown){b.flown=true;Logf("NPC_AIR %s is flown by the game (MOVE_Flying) at %.2fs: its arc is carried as a move",b.who.c_str(),clock-b.lifted_at);}
                Vector v=Property<Vector>(move,"Velocity");if(std::fabs(v.z)>0.01){v.z=0;Put(move,"Velocity",v);}
            }else{  // walking (put back by the game) or falling: falling at vz
                if(mode!=3)SetMoveMode(actor,3);
                Vector v=Property<Vector>(move,"Velocity");v.z=b.vz;Put(move,"Velocity",v);
            }
            Vector at{};
            if(ActorAt(actor,at)&&std::fabs(b.want_z-at.z)>0.5){  // moved back onto the arc (swept)
                const double dz=b.want_z-at.z,moved=AirShift(actor,at,b.want_z);
                ++b.kept;b.kept_cm+=std::fabs(moved);
                if(dz<-2&&b.vz<0&&moved>dz*0.5){SetMoveMode(actor,1);drop("landed (stopped on the way down)");continue;}
                if(dz>2&&moved<dz*0.5){  // a ceiling: the rise ends where it is
                    b.want_z=at.z+moved;if(b.vz>0)b.vz=0;
                    if(b.phase==1){b.phase=2;Logf("NPC_AIR %s stopped on the way up after %.2fs: %.0f cm over the start",b.who.c_str(),clock-b.rise_at,b.want_z-b.start_z);}}
            }
            ++i;continue;
        }
        if(mode==3)b.last_vz=Property<Vector>(move,"Velocity").z;
        if(mode!=1&&mode!=2&&mode!=3){char why[64]{};_snprintf_s(why,sizeof(why),_TRUNCATE,"movement mode %u took over",static_cast<unsigned>(mode));drop(why);continue;}
        if(b.phase==1){
            const double t=clock-b.rise_at;
            if(t<b.rise_s&&t<kAirRiseMaxS){
                if(mode!=3)SetMoveMode(actor,3);
                Vector v=Property<Vector>(move,"Velocity");const double want=b.v0-b.g*t;
                if(v.z<want-1){v.z=want;Put(move,"Velocity",v);}
                // no higher than the throw's own height by now (what else lifts it meanwhile is taken off; swept)
                const double arc=b.start_z+b.v0*t-0.5*b.g*t*t;Vector at{};
                if(ActorAt(actor,at)&&at.z>arc+2){const double moved=AirShift(actor,at,arc);++b.kept;b.kept_cm-=moved;b.top_z=(std::max)(top_before,at.z+moved);
                    if(v.z>want+1){v.z=want;Put(move,"Velocity",v);}}
                ++i;continue;}
            b.phase=2;
            Logf("NPC_AIR %s at the top after %.2fs: %.0f cm over the start%s",b.who.c_str(),t,b.top_z-b.start_z,b.kept?" (kept down to the throw's height)":"");
        }
        if(mode!=3){drop("landed");continue;}
        if(b.hang<=0.001){drop("not held");continue;}
        if(clock>b.until){drop("its hold is over");continue;}
        Put(move,"GravityScale",static_cast<float>(b.gravity*(1-b.hang)));
        if(!b.hanging){b.hanging=true;Vector v=Property<Vector>(move,"Velocity");if(v.z<0){v.z*=1-b.hang;Put(move,"Velocity",v);}}
        ++i;
    }
}
// ---- 滞空 / 空中释放 on the hero's own skills ---------------------------------------------------------------
// The game's own action of the hero (the chain played no move for its press) takes 滞空 / 空中释放 as NpcCombo::OwnAirFor
// picks them: the 滞空 a hero trigger of that action points at, else, cast in the air, the chain's 空中释放 for 影之武装 /
// 战技 / 特殊攻击. The hero is thrown up and held while the ability runs (AirLift, kept up every frame); each enemy whose
// health drops while it runs and kSkillRouteTailMs after it is thrown up and held as by a chain move (AirOnHit, once
// per skill). A press the chain takes has its ability cancelled the frame it starts, so an ability is taken one frame
// after it is first seen, when it still runs then; airborne: the hero was in the air when it was first seen. A skill
// runs while one of its abilities does (`handles`: the first and the ones of the same action it starts, such as the
// 影之武装 hook's pull or flight after its throw) or its montage plays (the hero's active montage in its first moments).
// own_skill_test_: npcretarget ownskill <bits hex>, the hero's first ability of those action bits that activates, as
// the game would start it (no input).
struct OwnAirSeen { int32_t handle=0;unsigned bits=0;Object* ability=nullptr;bool airborne=false; };
struct OwnAir { int32_t handle=0;std::vector<int32_t> handles;std::string name;unsigned bits=0;NpcCombo::AirSet air;int trigger=-1;ULONGLONG key=0,until=0,next_scan=0,started=0;
    std::vector<DamageTarget> targets;std::vector<Object*> lifted;uint32_t hits=0;bool lifted_self=false;Identity anim;Object* montage=nullptr; };
constexpr ULONGLONG kOwnAirMontageMs=300;  // how long after the start the hero's active montage is taken as the skill's
OwnAir own_air_;std::vector<int32_t> own_air_running_;std::vector<OwnAirSeen> own_air_new_;ULONGLONG own_air_serial_=0;
volatile LONG own_skill_test_=0;
void EndOwnAir(const char* why){
    if(own_air_.handle)Logf("NPC_AIR own skill %s done (%s): enemies hit %u times, %zu thrown up",own_air_.name.c_str(),why,own_air_.hits,own_air_.lifted.size());
    own_air_={};
}
void StartOwnAir(Object* pawn,const OwnAirSeen& seen,const NpcCombo::AirSet& air,int trigger){
    EndOwnAir("the next skill");
    const ULONGLONG now=GetTickCount64();
    own_air_.handle=seen.handle;own_air_.handles={seen.handle};own_air_.name=AbilityName(seen.ability);own_air_.bits=seen.bits;own_air_.air=air;own_air_.trigger=trigger;
    own_air_.started=now;{Context c;if(ContextNow(c))if(Object* anim=AnimInstance(c.mesh)){own_air_.anim=Id(anim);own_air_.montage=ActiveMontage(c);}}
    own_air_.key=0x8000000000000000ull|++own_air_serial_;own_air_.until=now+kSkillRouteTailMs;  // never a chain move's start (a tick)
    own_air_.lifted_self=(air.self_lift>0||air.self_hang>0)&&AirLift(pawn,air.self_lift,air.self_hang,ClockSeconds()+0.05,true,-1,own_air_.key,"the hero (own skill)");
    if(air.foe_lift>0||air.foe_hang>0){ScanTargets(pawn,pawn,own_air_.targets);own_air_.next_scan=now+kSkillDamageScanMs;}
    Logf("NPC_AIR own skill %s bits=0x%x cast %s (%s): hero +%dcm held %d%%%s; enemies hit +%dcm held %d%% (%zu around)",own_air_.name.c_str(),seen.bits,
         seen.airborne?"in the air":"on the ground",trigger>=0?"its trigger's 滞空 node":"air casts",air.self_lift,air.self_hang,
         own_air_.lifted_self||(!air.self_lift&&!air.self_hang)?"":" (not lifted: on the ground with no lift, or not walking / falling)",air.foe_lift,air.foe_hang,own_air_.targets.size());
}
void DriveOwnAir(){
    const Identity& pawn=ChainPawn();
    Object* asc=Same(pawn)?PawnAsc(pawn.object):nullptr;
    if(const LONG test=InterlockedExchange(&own_skill_test_,0);test&&ue::Alive(asc)){
        int32_t handle=0;std::string name;size_t tried=0;
        for(const auto& [h,ability]:Specs(asc)){
            if(!(AbilityActionBits(ability)&static_cast<unsigned>(test)))continue;
            ++tried;Call activate(asc,"TryActivateAbility");
            if(activate.Set("AbilityToActivate",h).Set("bAllowRemoteActivation",true).Invoke()&&activate.Get<bool>("ReturnValue")){handle=h;name=AbilityName(ability);break;}}
        Logf("NPC_TEST ownskill bits=0x%lx: %s (tried %zu, %s)",static_cast<unsigned long>(test),handle?("activated "+name).c_str():"none activated",tried,Airborne(pawn.object)?"in the air":"on the ground");
    }
    std::vector<HeroSpec> specs;
    const bool ready=combo_.owner!=-2&&combo_.config.enabled&&!Auditing()&&ue::Alive(asc)&&HeroActionSpecs(asc,specs)&&
                     borrow_.stage!=Borrow::Stage::Going&&borrow_.stage!=Borrow::Stage::Returning&&
                     weapon_.stage!=WeaponHold::Stage::Taking&&weapon_.stage!=WeaponHold::Stage::Returning;
    if(!ready){if(own_air_.handle)EndOwnAir("chain off");own_air_running_.clear();own_air_new_.clear();return;}
    const auto runs=[&](int32_t h){return std::any_of(specs.begin(),specs.end(),[&](const HeroSpec& x){return x.handle==h;});};
    const ULONGLONG now=GetTickCount64();
    if(own_air_.handle&&!own_air_.montage&&now<own_air_.started+kOwnAirMontageMs&&Same(own_air_.anim)){  // the skill's montage, once it is up
        Call current(own_air_.anim.object,"GetCurrentActiveMontage");Object* m=current.Invoke()?current.Get<Object*>("ReturnValue"):nullptr;if(ue::Alive(m))own_air_.montage=m;}
    const auto live=[&]{
        return own_air_.handle&&(std::any_of(own_air_.handles.begin(),own_air_.handles.end(),runs)||
                                 (own_air_.montage&&Same(own_air_.anim)&&MontagePlaying(own_air_.anim.object,own_air_.montage)));};
    const bool own_grab=(PlayingGrab()&&combo_.phase!=ComboRun::Phase::Idle)||GrabCutCleaning();
    const bool was_live=live();
    for(const OwnAirSeen& seen:own_air_new_){  // started last frame and not cancelled since: the game plays it
        if(!runs(seen.handle)||(skill_.handle>0&&seen.handle==skill_.handle)||(pending_.handle>0&&seen.handle==pending_.handle)||own_grab)continue;
        if(was_live&&(seen.bits&own_air_.bits)){  // the skill going on in another ability of its action: no second lift
            own_air_.handles.push_back(seen.handle);Logf("NPC_AIR own skill %s goes on in %s",own_air_.name.c_str(),AbilityName(seen.ability).c_str());continue;}
        int trigger=-1;const NpcCombo::AirSet air=NpcCombo::OwnAirFor(combo_.config,seen.bits,seen.airborne,&trigger);
        if(air.on){StartOwnAir(pawn.object,seen,air,trigger);break;}
    }
    own_air_new_.clear();
    const bool airborne=Airborne(pawn.object);
    for(const HeroSpec& x:specs)
        if(std::find(own_air_running_.begin(),own_air_running_.end(),x.handle)==own_air_running_.end())own_air_new_.push_back({x.handle,x.bits,x.ability,airborne});
    own_air_running_.clear();for(const HeroSpec& x:specs)own_air_running_.push_back(x.handle);
    if(!own_air_.handle)return;
    const double clock=ClockSeconds();
    const bool foes=own_air_.air.foe_lift>0||own_air_.air.foe_hang>0;
    if(live()){
        own_air_.until=now+kSkillRouteTailMs;
        for(AirBody& b:air_bodies_)if(b.hero&&b.started==own_air_.key)b.until=(std::max)(b.until,clock+0.05);
        if(own_air_.air.foe_hang>0)AirKeepFoes(own_air_.key);
    }else if(!foes||now>=own_air_.until){EndOwnAir("the skill is over");return;}
    if(!foes)return;
    if(now>=own_air_.next_scan){own_air_.next_scan=now+kSkillDamageScanMs;ScanTargets(pawn.object,pawn.object,own_air_.targets);}
    for(size_t i=0;i<own_air_.targets.size();){
        DamageTarget& t=own_air_.targets[i];float health=0;
        if(!HealthNow(t,health)){own_air_.targets.erase(own_air_.targets.begin()+static_cast<std::ptrdiff_t>(i));continue;}
        if(t.last-health>0.01f&&health>0&&!GrabExtras::Active()){
            const bool first=std::find(own_air_.lifted.begin(),own_air_.lifted.end(),t.pawn.object)==own_air_.lifted.end();
            if(first)own_air_.lifted.push_back(t.pawn.object);
            ++own_air_.hits;AirOnHit(t.pawn.object,first?own_air_.air.foe_lift:0,own_air_.air.foe_hang,own_air_.key);}
        t.last=health;++i;
    }
}
// The hero's 瞬身 dodge on the weapon in hand: its ability carrying the SpecialDash action tag ("" none).
std::string SpecialDashOf(Object* pawn){
    const unsigned bit=1u<<static_cast<int>(NpcCombo::Trigger::SpecialDash);
    for(const auto& [handle,ability]:Specs(PawnAsc(pawn))){(void)handle;if(AbilityActionBits(ability)&bit)return AbilityName(ability);}
    return {};
}
void PlanTeleport(Object* hero,const NpcCombo::Node& node){
    tp_.dist=NpcCombo::ClampTeleportDist(node.dist);tp_.lift=NpcCombo::ClampTeleportLift(node.lift);
    const char* via="lock";Object* target=LockedEnemy(hero,via);
    if(!target){target=NearestHostile(hero,kTeleportRangeCm);via=std::strcmp(via,"unread")==0?"nearest (lock unread)":"nearest";}
    if(!target){Logf("NPC_COMBO teleport target=none dist=%d z=%d ok=0 (no enemy within %.0f m: a plain 瞬身)",tp_.dist,tp_.lift,kTeleportRangeCm/100);return;}
    tp_.target=Text(target);
    Vector at{},hero_at{},facing{};double th=0,tr=0,hh=0,hr=0;
    {Call forward(target,"GetActorForwardVector");if(forward.Invoke())facing=forward.Get<Vector>("ReturnValue");}
    if(!ActorAt(target,at)||!ActorAt(hero,hero_at)||!CapsuleOf(target,th,tr)||!CapsuleOf(hero,hh,hr)){
        Logf("NPC_COMBO teleport target=%s via=%s dist=%d z=%d ok=0 (no position or capsule)",tp_.target.c_str(),via,tp_.dist,tp_.lift);return;}
    const int least=static_cast<int>(std::ceil(tr+hr+20));const char* why="no-ground";int used=tp_.dist;
    std::vector<Object*> skip=TraceIgnore(hero,target);
    // Straight in front first (every distance down to the closest), then swung round the target 30 / 60 / 90 degrees
    // either way (the wanted distance, halfway, the closest) when the front is walled off, bare or taken.
    const std::vector<int> all=NpcCombo::TeleportDistances(tp_.dist,least);
    std::vector<int> side{all.front()};if(all.size()>2)side.push_back(all[all.size()/2]);if(all.size()>1)side.push_back(all.back());
    int turned=0;
    for(const double turn:{0.0,30.0,-30.0,60.0,-60.0,90.0,-90.0}){
        for(const int d:turn==0?all:side){
            used=d;
            const NpcCombo::Landing l=NpcCombo::LandingAt(at.x,at.y,facing.x,facing.y,hero_at.x,hero_at.y,d,turn);
            Vector ground{},normal{};
            if(TraceLine(hero,Vector{l.x,l.y,at.z+th},Vector{l.x,l.y,at.z-th-400},skip,&ground,&normal)!=1||!(normal.z>=0.7)){why="no-ground";continue;}
            if(TraceLine(hero,at,Vector{l.x,l.y,ground.z+hh},skip)!=0){why="wall";continue;}
            // On a slope the capsule's round bottom needs that much more height to clear the ground.
            const double clear=2+hr*(1/normal.z-1);
            const Vector centre{l.x,l.y,ground.z+hh+tp_.lift+clear};
            if(TraceCapsule(hero,centre,hh,hr,skip)!=0){why="no-room";continue;}
            tp_.ok=true;tp_.dest=centre;tp_.yaw=l.yaw;turned=static_cast<int>(turn);break;
        }
        if(tp_.ok)break;
    }
    tp_.goal=Id(target);
    Logf("NPC_COMBO teleport target=%s via=%s dist=%d z=%d side=%d ok=%d%s%s at=(%.0f,%.0f,%.0f) yaw=%.0f",tp_.target.c_str(),via,used,tp_.lift,turned,tp_.ok?1:0,
         tp_.ok?"":" reason=",tp_.ok?"":why,tp_.dest.x,tp_.dest.y,tp_.dest.z,tp_.yaw);
}
// The hero hidden by its own dodge (the hero look only: a look's display mesh is ours; our own blink is no 瞬身).
bool HiddenByGame(Object* pawn){
    if(active_||blink_.on)return false;
    bool hidden=false;if(Bool(pawn,"bHidden",hidden)&&hidden)return true;
    bool visible=true,off=false;Object* mesh=Property<Object*>(pawn,"Mesh");
    return VisibilityOf(mesh,visible,off)&&(!visible||off);
}
// On the landing: the first time through the game's fit (K2_TeleportTo), then kept there; the last time facing the
// target with the dodge's speed taken away (from `lift` cm up the hero falls).
void TeleportPut(bool first,bool last){
    Object* pawn=tp_.pawn.object;if(!Same(tp_.pawn))return;
    RotatorD turn;turn.yaw=tp_.yaw;
    if(first){
        Call tp(pawn,"K2_TeleportTo");
        const bool ok=tp.Set("DestLocation",tp_.dest).Set("DestRotation",turn).Invoke()&&tp.Get<bool>("ReturnValue");
        Vector actual{};if(ok&&ActorAt(pawn,actual))tp_.dest=actual;  // where the game fitted the hero
        Logf("NPC_COMBO teleport placed node=%d ok=%d at=(%.0f,%.0f,%.0f) hide=%s",tp_.node,ok?1:0,tp_.dest.x,tp_.dest.y,tp_.dest.z,tp_.native?"game":"quiet");
    } else Call(pawn,"K2_SetActorLocationAndRotation").Set("NewLocation",tp_.dest).Set("NewRotation",turn).Set("bSweep",false).Set("bTeleport",true).Invoke();
    if(last){Object* movement=Property<Object*>(pawn,"CharacterMovement");if(ue::Alive(movement))Call(movement,"StopMovementImmediately").Invoke();}
}
void EndTeleport(const char* why){
    if(tp_.stage==TeleportRun::Stage::Off)return;
    if(tp_.stage==TeleportRun::Stage::Holding){TeleportPut(false,true);Logf("NPC_COMBO teleport node=%d landed early (%s)",tp_.node,why);}
    if(tp_.quiet)BlinkShowNow();
    tp_={};
}
void StartTeleport(const Context& context,int index,int button,ULONGLONG now){
    const NpcCombo::Node* node=ComboNode(index);if(!node)return;
    combo_.cursor=index;combo_.playing=index;combo_.button=button;combo_.phase=ComboRun::Phase::Starting;combo_.at=now;combo_.cut_at=0;
    combo_.rated=nullptr;combo_.first_rated=false;combo_.rate_check=0;
    EndTeleport("next-teleport");
    tp_={};tp_.stage=TeleportRun::Stage::Done;tp_.node=index;tp_.pawn=Id(context.pawn);tp_.since=now;
    PlanTeleport(context.pawn,*node);
    tp_.ability=SpecialDashOf(context.pawn);
    if(tp_.ability.empty()){Logf("NPC_COMBO teleport node=%d: the hero has no 瞬身 dodge now",index);TeleportRefused(now);PublishCombo(L"派生链播放中：瞬移");return;}
    combo_.before=ActiveMontage(context);
    if(!CastHeroMove(context,tp_.ability,L"瞬移",kChainWaitMs)){TeleportRefused(now);PublishCombo(L"派生链播放中：瞬移");return;}
    if(tp_.ok)tp_.stage=TeleportRun::Stage::Waiting;
    Logf("NPC_COMBO play node=%d button=%d trigger=%d teleport dash=%s%s",index,button,combo_.trigger,tp_.ability.c_str(),tp_.ok?"":" (no landing: a plain 瞬身)");
    PublishCombo(L"派生链播放中：瞬移");
}
// No 瞬身 to cast (none on this weapon, or the game refused it now): the blink alone covers the move. false when the
// playing node is no 瞬移.
bool TeleportRefused(ULONGLONG now){
    if(!PlayingTeleport())return false;
    if(pending_.handle>0)DropPendingSkill("teleport-blink");
    if(tp_.ok&&Same(tp_.pawn)&&!Auditing()){
        tp_.stage=TeleportRun::Stage::Holding;BlinkOut(tp_.pawn.object);TeleportPut(true,true);BlinkDone();
        Logf("NPC_COMBO teleport node=%d blink only (no 瞬身 cast)",tp_.node);
    } else Logf("NPC_COMBO teleport node=%d skipped: no 瞬身 and no landing",tp_.node);
    tp_.stage=TeleportRun::Stage::Done;combo_.phase=ComboRun::Phase::Running;combo_.started=now;
    return true;
}
// Every frame: hide seen (or waited for) -> on the landing, kept there while hidden -> shown, facing the target.
void DriveTeleport(){
    if(tp_check_.at&&GetTickCount64()>=tp_check_.at){
        Vector actual{},goal{};
        if(Same(tp_check_.pawn)&&ActorAt(tp_check_.pawn.object,actual)){
            const double off=std::hypot(actual.x-tp_check_.dest.x,actual.y-tp_check_.dest.y);
            const double to=Same(tp_check_.goal)&&ActorAt(tp_check_.goal.object,goal)?std::hypot(actual.x-goal.x,actual.y-goal.y):-1;
            Logf("NPC_COMBO teleport node=%d 300ms after landing: at=(%.0f,%.0f,%.0f) off-landing=%.0fcm to-target=%.0fcm",tp_check_.node,actual.x,actual.y,actual.z,off,to);}
        tp_check_={};
    }
    if(!TeleportBusy())return;
    if(!Same(tp_.pawn)){tp_={};return;}
    const ULONGLONG now=GetTickCount64();
    if(tp_.stage==TeleportRun::Stage::Waiting){
        if(combo_.playing!=tp_.node||combo_.trigger<0){EndTeleport("node-left");return;}
        if(combo_.phase!=ComboRun::Phase::Running)return;  // the dodge has not started yet
        if(HiddenByGame(tp_.pawn.object))tp_.native=true;
        else if(now<combo_.started+(active_?kTeleportHideWaitMs:kTeleportNativeWaitMs))return;
        else{BlinkOut(tp_.pawn.object,false);tp_.quiet=blink_.on;}
        TeleportPut(true,false);tp_.stage=TeleportRun::Stage::Holding;tp_.hold_until=now+(tp_.native?kTeleportHoldMaxMs:kTeleportQuietHoldMs);
        return;
    }
    const bool hidden=tp_.native?HiddenByGame(tp_.pawn.object):true;
    if(hidden&&now<tp_.hold_until){TeleportPut(false,false);return;}
    TeleportPut(false,true);
    // The dash's montage would carry the hero on from the landing (root motion): it is stopped there.
    bool stopped=false;
    if(Object* anim=AnimInstance(Property<Object*>(tp_.pawn.object,"Mesh"))){Call playing(anim,"IsAnyMontagePlaying");
        if(playing.Slot("ReturnValue",sizeof(bool))&&playing.Invoke()&&playing.Get<bool>("ReturnValue"))
            stopped=Call(anim,"Montage_Stop").Set("InBlendOutTime",0.15f).Set("Montage",static_cast<Object*>(nullptr)).Invoke();}
    if(tp_.quiet)BlinkShowNow();
    Logf("NPC_COMBO teleport landed node=%d hide=%s dash-montage-stopped=%d",tp_.node,tp_.native?"game":"quiet",stopped?1:0);
    tp_check_={tp_.pawn,tp_.goal,tp_.dest,tp_.node,now+300};
    tp_.stage=TeleportRun::Stage::Done;tp_.quiet=false;
}
// ---- 钩子 自动 --------------------------------------------------------------------------------------------
// 破防: the game's broken stance on an enemy (CharacterState.Stand.Break and what is under it, the state the AI trees
// test; npcretarget tagdump shows a target's tags). A target in it is pulled over, any other the hero goes to.
constexpr const wchar_t* kGuardBrokenTags[]={L"CharacterState.Stand.Break",L"CharacterState.Hitreact.State.Break"};
// The enemy's hit reaction state (HitreactComponent.GetCurrentHitState, e.g. CharacterState.Hitreact.State.Lying); "" none.
std::string HitStateOf(Object* enemy){
    Object* cls=Class("/Script/SGameCombatSystem.HitreactComponent");if(!cls||!ue::Alive(enemy))return {};
    Call get(enemy,"GetComponentByClass");Object* comp=get.Set("ComponentClass",cls).Invoke()?get.Get<Object*>("ReturnValue"):nullptr;if(!ue::Alive(comp))return {};
    Call state(comp,"GetCurrentHitState");if(!state.Invoke())return {};const Name n=state.Get<Name>("ReturnValue");return n.None()?std::string():Text(n);
}
bool GuardBroken(Object* enemy,std::string& which){
    // A Break hit state (破防's own reaction: Hitreact.Strength.Income.Break.*), not an endure level's name.
    const std::string state=HitStateOf(enemy);
    if(state.find(".Break")!=std::string::npos&&state.find("Endure")==std::string::npos){which="hit state "+state;return true;}
    Object* asc=ue::Alive(enemy)?PawnAsc(enemy):nullptr;if(!asc){which="no ability system";return false;}
    for(const wchar_t* tag:kGuardBrokenTags)if(LooseTagOn(asc,tag)){which.clear();for(const wchar_t* c=tag;*c;++c)which.push_back(static_cast<char>(*c));return true;}
    return false;
}
// The 钩子's target before the cast: the locked enemy, else the nearest (AimHook then locks it).
Object* HookTarget(Object* hero){const char* via="lock";Object* t=LockedEnemy(hero,via);return t?t:NearestHostile(hero,kTeleportRangeCm);}
// Tests: npcretarget tagdump <ms>: the 钩子 target's owned gameplay tags whenever they change (every 100 ms).
volatile LONG tag_dump_ms_=0;ULONGLONG tag_dump_until_=0,tag_dump_next_=0;std::string tag_dump_last_;
void DriveTagDump(){
    if(const LONG ms=InterlockedExchange(&tag_dump_ms_,0)){tag_dump_until_=GetTickCount64()+static_cast<ULONGLONG>(ms);tag_dump_last_.clear();}
    if(!tag_dump_until_)return;const ULONGLONG now=GetTickCount64();
    if(now>=tag_dump_until_){tag_dump_until_=0;Logf("NPC_TEST tagdump off");return;}
    if(now<tag_dump_next_)return;tag_dump_next_=now+100;
    Context c;if(!ContextNow(c))return;
    Object* t=HookTarget(c.pawn);Object* asc=t?PawnAsc(t):nullptr;if(!asc){if(tag_dump_last_!="-"){tag_dump_last_="-";Logf("NPC_TEST tagdump: no target");}return;}
    // The owned tags through the tag library (the ability system's own getter is not reflected in this build), and
    // which of a few likely state tags (and their parents) match.
    std::string text;
    for(const char* param:{"Object","TargetObject"}){
        Call get(ue::Default("/Script/GameplayTags.Default__BlueprintGameplayTagLibrary"),"GetOwnedGameplayTags");unsigned char* r=get.Slot("ReturnValue",32);
        if(!r||!get.Set(param,t).Invoke())continue;
        Array tags{},parents{};std::memcpy(&tags,r,sizeof(Array));std::memcpy(&parents,r+sizeof(Array),sizeof(Array));
        if(tags.num>0&&tags.num<1024&&tags.data)for(int32_t i=0;i<tags.num;++i)text+=" "+Text(static_cast<const Name*>(tags.data)[i]);
        ue::Free(tags.data);ue::Free(parents.data);text=" owned:"+text;break;}
    static constexpr const wchar_t* kProbe[]={L"CharacterState.Stand.Break",L"CharacterState.Stand",L"CharacterState.Break",L"CharacterState.HitReact",
        L"CharacterState.Hitreact",L"CharacterState.HitReact.State",L"CharacterState.HitReact.State.Break",L"CharacterState.Weak",L"CharacterState.Stun",
        L"CharacterState.Down",L"CharacterState.Execution",L"CharacterState.CanBeExecuted",L"CharacterState.Posture",L"Hitreact",L"Hitreact.Break",
        L"Hitreact.Strength.Income.Break",L"Hitreact.State",L"Hitreact.State.Break",L"State.Break",L"CharacterState"};
    text+=" matches:";
    for(const wchar_t* tag:kProbe)if(LooseTagOn(asc,tag)){text+=" ";for(const wchar_t* ch=tag;*ch;++ch)text.push_back(static_cast<char>(*ch));}
    std::string which;const bool broken=GuardBroken(t,which);text+=" hitState="+HitStateOf(t);
    if(text!=tag_dump_last_){tag_dump_last_=text;Logf("NPC_TEST tagdump %s broken=%d:%s",Text(t).c_str(),broken?1:0,text.c_str());}
}
// Tests: npcretarget abilityprobe <frames>: what of the running skill's ability changes while it plays (the clock its
// timeline keeps). The ability, its Lego timelines, its active tasks and the objects it owns are copied when a skill is
// first seen running and twice more <frames> apart; each 4-byte float and 8-byte double that moved in both spans the
// same way goes to the log with the reflected field it sits in, the real time between the copies and the montage's
// position at each.
volatile LONG ability_probe_frames_=0;
struct AbilityProbe { struct Snap { Object* object=nullptr;std::string label;std::vector<unsigned char> bytes[3]; };
                      int32_t handle=0;int frames=0,wait=0,taken=0;double clock[3]{};float montage_at[3]{};Object* montage=nullptr;std::vector<Snap> snaps; };
AbilityProbe ability_probe_;
std::string FieldAt(Object* object,int32_t offset){
    std::vector<ue::Field> f(1024);
    for(Object* s=ue::ClassOf(object);s;s=ue::At<Object*>(s,static_cast<int32_t>(ue::layout::kSuper))){
        const size_t n=ue::Fields(s,f.data(),f.size());
        for(size_t i=0;i<n;++i)if(offset>=f[i].offset&&offset<f[i].offset+(std::max)(1,f[i].size))
            return Text(s)+"."+f[i].name+(offset>f[i].offset?"+"+std::to_string(offset-f[i].offset):"");
    }
    return "native";
}
// Every reflected field of an object's class chain below UObject with its value (1/4/8-byte fields as numbers,
// object fields by name), for the probe's tasks.
void ProbeFields(Object* object,const std::string& label){
    std::vector<ue::Field> f(512);std::string line;int count=0;
    for(Object* s=ue::ClassOf(object);s;s=ue::At<Object*>(s,static_cast<int32_t>(ue::layout::kSuper))){
        if(ue::NameIs(ue::NameOf(s),"Object"))break;
        const size_t n=ue::Fields(s,f.data(),f.size());line+=" ["+Text(s)+"]";
        for(size_t i=0;i<n;++i){const ue::Field& x=f[i];char v[96]{};
            if(x.offset<0||!FaceReadable(object+x.offset,static_cast<size_t>((std::max)(1,x.size))))continue;
            if(x.size==4)_snprintf_s(v,sizeof(v),_TRUNCATE,"%d/%.4g",ue::At<int32_t>(object,x.offset),static_cast<double>(ue::At<float>(object,x.offset)));
            else if(x.size==8){Object* o=ue::At<Object*>(object,x.offset);const std::string lower=Lower(x.name);
                if(o&&ue::Alive(o))_snprintf_s(v,sizeof(v),_TRUNCATE,"%s",Text(o).c_str());
                else if(lower.find("name")!=std::string::npos||lower.find("socket")!=std::string::npos||lower.find("bone")!=std::string::npos||lower.find("tag")!=std::string::npos)
                    _snprintf_s(v,sizeof(v),_TRUNCATE,"'%s'",Text(ue::At<Name>(object,x.offset)).c_str());
                else _snprintf_s(v,sizeof(v),_TRUNCATE,"%lld/%.4g",ue::At<long long>(object,x.offset),ue::At<double>(object,x.offset));}
            else if(x.size==1)_snprintf_s(v,sizeof(v),_TRUNCATE,"%u",ue::At<uint8_t>(object,x.offset));
            else _snprintf_s(v,sizeof(v),_TRUNCATE,"[%d]",x.size);
            line+=" "+std::string(x.name)+"@0x"+[&]{char h[16];_snprintf_s(h,sizeof(h),_TRUNCATE,"%x",x.offset);return std::string(h);}()+"="+v;
            if(line.size()>700){Logf("NPC_TEST abilityprobe fields %s:%s",label.c_str(),line.c_str());line.clear();}
            ++count;}
    }
    if(!line.empty()||!count)Logf("NPC_TEST abilityprobe fields %s:%s",label.c_str(),line.c_str());
}
bool probe_warp_fields_=true;
// The functions a class chain declares (its Children), from the class down to LegoAbility.
void ProbeFunctions(Object* cls){
    for(Object* s=cls;s;s=ue::At<Object*>(s,static_cast<int32_t>(ue::layout::kSuper))){
        if(ue::NameIs(ue::NameOf(s),"GameplayAbility"))break;
        std::string line;int n=0;
        const bool params=s==cls||ue::NameIs(ue::NameOf(s),"LegoAbility");  // the move's own class and the Lego base: with their parameters
        for(Object* f=ue::At<Object*>(s,static_cast<int32_t>(ue::layout::kChildren));f&&n<400;f=ue::At<Object*>(f,static_cast<int32_t>(ue::layout::kFieldNext)),++n){
            line+=" "+Text(ue::NameOf(f));
            if(params){ue::Field a[24];const size_t k=ue::Fields(f,a,std::size(a));if(k){line+="(";for(size_t j=0;j<k;++j)line+=(j?",":"")+std::string(a[j].name)+":"+std::to_string(a[j].size);line+=")";}}
            if(line.size()>800){Logf("NPC_TEST abilityprobe functions %s:%s",Text(s).c_str(),line.c_str());line.clear();}}
        Logf("NPC_TEST abilityprobe functions %s (%d):%s",Text(s).c_str(),n,line.c_str());
    }
}
// The rows of a timeline: each action's class, start and length (LGAActionEvent StartTime / Duration).
void ProbeActions(Object* timeline,const std::string& label){
    static Object* const event_struct=Class("/Script/LegoAbility.LGAActionEvent");if(!event_struct)return;
    const int32_t actions_at=Offset(timeline,"Actions"),action_at=ue::Offset(event_struct,"Action"),start_at=ue::Offset(event_struct,"StartTime"),
        length_at=ue::Offset(event_struct,"Duration"),stride=ue::StructSize(event_struct);
    if(actions_at<0||action_at<0||stride<=action_at||!FaceReadable(timeline+actions_at,sizeof(Array)))return;
    const Array actions=ue::At<Array>(timeline,actions_at);
    if(actions.num<=0||actions.num>1024||!FaceReadable(actions.data,static_cast<size_t>(actions.num)*static_cast<size_t>(stride)))return;
    std::string line;static Object* const warp_class=Class("/Script/LegoAbility.LGAAction_MotionWarping_Base");
    for(int32_t k=0;k<actions.num;++k){Object* row=static_cast<Object*>(actions.data)+static_cast<size_t>(k)*static_cast<size_t>(stride);Object* a=ue::At<Object*>(row,action_at);
        if(ue::Alive(a)&&warp_class&&ue::IsA(a,warp_class)&&probe_warp_fields_)ProbeFields(a,label+" warp action "+std::to_string(k));
        char v[160]{};_snprintf_s(v,sizeof(v),_TRUNCATE," %s@%.2f+%.2f",ue::Alive(a)?Text(ue::ClassOf(a)).c_str():"-",start_at>=0?static_cast<double>(ue::At<float>(row,start_at)):-1.0,
                                  length_at>=0?static_cast<double>(ue::At<float>(row,length_at)):-1.0);line+=v;
        if(line.size()>700){Logf("NPC_TEST abilityprobe actions %s:%s",label.c_str(),line.c_str());line.clear();}}
    if(!line.empty())Logf("NPC_TEST abilityprobe actions %s:%s",label.c_str(),line.c_str());
}
// The hero's motion warp targets (MotionWarpingComponent.WarpTargets: name and location), at each copy.
void ProbeWarps(Object* pawn,int k){
    static Object* const warp_class=Class("/Script/MotionWarping.MotionWarpingComponent"),*target_struct=Class("/Script/MotionWarping.MotionWarpingTarget");
    if(!warp_class||!target_struct){Logf("NPC_TEST abilityprobe warps: no MotionWarping classes");return;}
    Call get(pawn,"GetComponentByClass");Object* warp=get.Set("ComponentClass",warp_class).Invoke()?get.Get<Object*>("ReturnValue"):nullptr;
    if(!ue::Alive(warp)){Logf("NPC_TEST abilityprobe warps %d: the hero has no MotionWarpingComponent",k);return;}
    const int32_t list_at=Offset(warp,"WarpTargets"),name_at=ue::Offset(target_struct,"Name"),at_at=ue::Offset(target_struct,"Location"),stride=ue::StructSize(target_struct);
    if(list_at<0||name_at<0||at_at<0||stride<=0)return;
    const Array list=ue::At<Array>(warp,list_at);std::string line;
    if(list.num>=0&&list.num<=32&&(!list.num||FaceReadable(list.data,static_cast<size_t>(list.num)*static_cast<size_t>(stride))))
        for(int32_t i=0;i<list.num;++i){Object* row=static_cast<Object*>(list.data)+static_cast<size_t>(i)*static_cast<size_t>(stride);const Vector v=ue::At<Vector>(row,at_at);
            char t[160]{};_snprintf_s(t,sizeof(t),_TRUNCATE," %s=(%.0f,%.0f,%.0f)",Text(ue::At<Name>(row,name_at)).c_str(),v.x,v.y,v.z);line+=t;}
    Vector a{};ActorAt(pawn,a);
    Logf("NPC_TEST abilityprobe warps %d: hero at (%.0f,%.0f,%.0f), %d targets:%s",k,a.x,a.y,a.z,list.num,line.c_str());
}
void ProbeCopy(AbilityProbe::Snap& s,int k){
    const int32_t size=ue::Alive(s.object)?(std::min)(ue::StructSize(ue::ClassOf(s.object)),0x4000):0;
    s.bytes[k].assign(static_cast<size_t>(size>0?size:0),0);
    if(size>0&&FaceReadable(s.object,static_cast<size_t>(size)))std::memcpy(s.bytes[k].data(),s.object,static_cast<size_t>(size));
}
void DriveAbilityProbe(){
    if(const LONG f=InterlockedExchange(&ability_probe_frames_,0)){ability_probe_={};ability_probe_.frames=static_cast<int>(f);Logf("NPC_TEST abilityprobe armed: %d frames apart",ability_probe_.frames);}
    AbilityProbe& p=ability_probe_;if(p.frames<=0)return;
    if(p.taken==0){
        if(skill_.handle<=0||!Same(skill_.asc))return;
        bool instance=false;Object* ability=SpecAbility(skill_.asc.object,skill_.handle,instance);if(!instance||!ue::Alive(ability))return;
        p.handle=skill_.handle;
        p.snaps.push_back({ability,"ability "+Text(ability)});
        static Object* const timeline_class=Class("/Script/LegoAbility.LGATimeline"),*action_class=Class("/Script/LegoAbility.LGAAction");
        int32_t timelines_at=Offset(ability,"Timelines");if(timelines_at<0)timelines_at=0x400;
        if(FaceReadable(ability+timelines_at,sizeof(Array))){const Array t=ue::At<Array>(ability,timelines_at);
            if(t.num>0&&t.num<=16&&FaceReadable(t.data,static_cast<size_t>(t.num)*8))
                for(int32_t i=0;i<t.num;++i){Object* o=static_cast<Object**>(t.data)[i];if(ue::Alive(o))p.snaps.push_back({o,"timeline "+Text(o)+" ("+Text(ue::ClassOf(o))+")"});}}
        const int32_t tasks_at=Offset(ability,"ActiveTasks");
        if(tasks_at>=0&&FaceReadable(ability+tasks_at,sizeof(Array))){const Array t=ue::At<Array>(ability,tasks_at);
            if(t.num>0&&t.num<=32&&FaceReadable(t.data,static_cast<size_t>(t.num)*8))
                for(int32_t i=0;i<t.num;++i){Object* o=static_cast<Object**>(t.data)[i];if(ue::Alive(o))p.snaps.push_back({o,"task "+Text(o)+" ("+Text(ue::ClassOf(o))+")"});}}
        std::map<std::string,int> owned;int actions=0;
        static Object* const task_class=Class("/Script/GameplayAbilities.AbilityTask");const int32_t task_ability_at=task_class?ue::Offset(task_class,"Ability"):-1;
        ue::ForEachObject([&](Object* o){
            if(task_class&&task_ability_at>=0&&ue::IsA(o,task_class)&&!ue::IsTemplate(o)&&ue::At<Object*>(o,task_ability_at)==ability){
                if(p.snaps.size()<48&&std::none_of(p.snaps.begin(),p.snaps.end(),[&](const AbilityProbe::Snap& x){return x.object==o;}))
                    p.snaps.push_back({o,"task "+Text(o)+" ("+Text(ue::ClassOf(o))+", by its Ability)"});
                return true;}
            if(ue::OuterOf(o)!=ability||ue::IsTemplate(o))return true;
            if(action_class&&ue::IsA(o,action_class)){++actions;return true;}
            if(timeline_class&&ue::IsA(o,timeline_class))return true;
            ++owned[Text(ue::ClassOf(o))];
            if(p.snaps.size()<48&&std::none_of(p.snaps.begin(),p.snaps.end(),[&](const AbilityProbe::Snap& x){return x.object==o;}))p.snaps.push_back({o,"owned "+Text(o)+" ("+Text(ue::ClassOf(o))+")"});
            return true;});
        std::string list;for(const auto& [k,v]:owned)list+=" "+k+"x"+std::to_string(v);
        std::string chain;for(Object* s=ue::ClassOf(ability);s;s=ue::At<Object*>(s,static_cast<int32_t>(ue::layout::kSuper)))chain+=" "+Text(s);
        Logf("NPC_TEST abilityprobe %s (%s): %zu objects copied, %d timeline actions;%s; classes:%s",skill_.name.c_str(),Text(ue::ClassOf(ability)).c_str(),p.snaps.size(),actions,
             list.c_str(),chain.c_str());
        for(const AbilityProbe::Snap& x:p.snaps){
            if(x.label.rfind("task ",0)==0)ProbeFields(x.object,x.label);
            else if(x.label.rfind("timeline ",0)==0)ProbeActions(x.object,x.label);}
        ProbeFunctions(ue::ClassOf(ability));
        ProbeFields(ability,"ability "+Text(ability));
    }else if(skill_.handle!=p.handle){Logf("NPC_TEST abilityprobe: the skill ended after %d copies",p.taken);p={};return;}
    else if(++p.wait<p.frames)return;
    p.wait=0;const int k=p.taken;
    for(AbilityProbe::Snap& s:p.snaps)ProbeCopy(s,k);
    p.clock[k]=ClockSeconds();
    Context c;Object* montage=ContextNow(c)?ActiveMontage(c):nullptr;if(k==0)p.montage=montage;
    p.montage_at[k]=montage&&montage==p.montage?MontagePosition(AnimInstance(c.mesh),montage):-1.f;
    if(ue::Alive(c.pawn))ProbeWarps(c.pawn,k);
    if(++p.taken<3)return;
    Logf("NPC_TEST abilityprobe copies %.3fs and %.3fs apart; %s at %.3f / %.3f / %.3f",p.clock[1]-p.clock[0],p.clock[2]-p.clock[1],Text(p.montage).c_str(),p.montage_at[0],p.montage_at[1],p.montage_at[2]);
    const auto ok=[](double v){return std::isfinite(v)&&std::fabs(v)<1e6;};
    for(AbilityProbe::Snap& s:p.snaps){
        const size_t size=(std::min)({s.bytes[0].size(),s.bytes[1].size(),s.bytes[2].size()});int lines=0;
        for(size_t o=0x28;o+4<=size&&lines<40;o+=4){
            float a,b,cc;std::memcpy(&a,&s.bytes[0][o],4);std::memcpy(&b,&s.bytes[1][o],4);std::memcpy(&cc,&s.bytes[2][o],4);
            if(ok(a)&&ok(b)&&ok(cc)&&a!=b&&b!=cc&&((b-a)>0)==((cc-b)>0)&&std::fabs(b-a)>1e-5){
                ++lines;Logf("NPC_TEST abilityprobe %s +0x%zx %s float %.4f -> %.4f -> %.4f",s.label.c_str(),o,FieldAt(s.object,static_cast<int32_t>(o)).c_str(),a,b,cc);}
            if(o%8==0&&o+8<=size){double da,db,dc;std::memcpy(&da,&s.bytes[0][o],8);std::memcpy(&db,&s.bytes[1][o],8);std::memcpy(&dc,&s.bytes[2][o],8);
                if(ok(da)&&ok(db)&&ok(dc)&&da!=db&&db!=dc&&((db-da)>0)==((dc-db)>0)&&std::fabs(db-da)>1e-6&&std::fabs(da)>1e-30){
                    ++lines;Logf("NPC_TEST abilityprobe %s +0x%zx %s double %.4f -> %.4f -> %.4f",s.label.c_str(),o,FieldAt(s.object,static_cast<int32_t>(o)).c_str(),da,db,dc);}}
        }
        if(!lines)Logf("NPC_TEST abilityprobe %s: %zu bytes, nothing moved",s.label.c_str(),size);
    }
    p={};
}
// Tests: npcretarget timelinewatch <ms>: the running skill's timeline tasks (which timeline, its time) whenever the set
// changes, with the hero's place and movement mode, every frame for that long.
volatile LONG timeline_watch_ms_=0;ULONGLONG timeline_watch_until_=0;std::string timeline_watch_last_;
volatile LONG action_dump_=0,aim_test_=0;double aim_test_pitch_=0,aim_test_yaw_=0;  // tests: npcretarget actiondump: the running skill's actions whose window holds its timeline's time now
void DumpLiveActions(){
    if(skill_.handle<=0||!Same(skill_.asc))return;bool instance=false;Object* ability=SpecAbility(skill_.asc.object,skill_.handle,instance);if(!instance||!ue::Alive(ability))return;
    InterlockedExchange(&action_dump_,0);
    static Object* const task_class=Class("/Script/LegoAbility.LGATask_PlayTimeline"),*event_struct=Class("/Script/LegoAbility.LGAActionEvent");
    const int32_t time_at=task_class?ue::Offset(task_class,"CurrentTime"):-1,line_at=task_class?ue::Offset(task_class,"Timeline"):-1,tasks_at=Offset(ability,"ActiveTasks");
    if(!event_struct||time_at<0||line_at<0||tasks_at<0)return;
    const int32_t action_at=ue::Offset(event_struct,"Action"),start_at=ue::Offset(event_struct,"StartTime"),length_at=ue::Offset(event_struct,"Duration"),stride=ue::StructSize(event_struct);
    const Array tasks=ue::At<Array>(ability,tasks_at);if(tasks.num<=0||tasks.num>64)return;
    for(int32_t i=0;i<tasks.num;++i){Object* t=static_cast<Object**>(tasks.data)[i];if(!ue::Alive(t)||!ue::IsA(t,task_class))continue;
        Object* line=ue::At<Object*>(t,line_at);const float now=ue::At<float>(t,time_at);const int32_t actions_at=ue::Alive(line)?Offset(line,"Actions"):-1;if(actions_at<0)continue;
        const Array actions=ue::At<Array>(line,actions_at);if(actions.num<=0||actions.num>1024)continue;int shown=0;
        Logf("NPC_TEST actiondump %s at %.3f",Text(line).c_str(),now);
        for(int32_t k=0;k<actions.num&&shown<8;++k){Object* row=static_cast<Object*>(actions.data)+static_cast<size_t>(k)*static_cast<size_t>(stride);
            const float a=ue::At<float>(row,start_at),d=ue::At<float>(row,length_at);Object* act=ue::At<Object*>(row,action_at);
            if(!ue::Alive(act)||d<=0||now<a||now>a+d)continue;++shown;
            ProbeFields(act,Text(ue::ClassOf(act))+"@"+std::to_string(a).substr(0,4)+"+"+std::to_string(d).substr(0,4));
            const int32_t row_size=stride;std::string hex;for(int32_t b=0;b<row_size&&b<96;b+=4){char h[12];_snprintf_s(h,sizeof(h),_TRUNCATE,"%08x ",ue::At<uint32_t>(row,b));hex+=h;}
            Logf("NPC_TEST actiondump row %d: %s",k,hex.c_str());}}
}
void DriveTimelineWatch(){
    if(action_dump_)DumpLiveActions();
    if(aim_test_==2){InterlockedExchange(&aim_test_,0);Context c;
        if(ContextNow(c))if(Object* pc=PlayerController(c.pawn)){Call get(pc,"GetControlRotation");const RotatorD keep=get.Invoke()?get.Get<RotatorD>("ReturnValue"):RotatorD{};
            const double base=ActorYaw(c.pawn);
            for(int k=0;k<12;++k){const RotatorD r{aim_test_pitch_,base+30.0*k,0};Call(pc,"SetControlRotation").Set("NewRotation",r).Invoke();
                // The camera follows the control rotation on its own tick: the trace starts 3 m behind the hero instead.
                constexpr double kRad=3.14159265358979323846/180.0;
                const Vector want{std::cos(r.pitch*kRad)*std::cos(r.yaw*kRad),std::cos(r.pitch*kRad)*std::sin(r.yaw*kRad),std::sin(r.pitch*kRad)};
                Vector at{};ActorAt(c.pawn,at);std::vector<Object*> skip=TraceIgnore(c.pawn,nullptr);Vector point{},normal{};
                const Vector from{at.x-want.x*300,at.y-want.y*300,at.z+60-want.z*300};
                const Vector to{from.x+want.x*1850,from.y+want.y*1850,from.z+want.z*1850};
                const int hit=TraceLine(c.pawn,from,to,skip,&point,&normal);
                Logf("NPC_TEST aimprobe heading +%d deg pitch %.0f: %s at %.0f cm from the hero, normal (%.2f,%.2f,%.2f)",30*k,r.pitch,hit==1?(normal.z>=0.7?"ground":normal.z<=-0.7?"ceiling":"wall"):"nothing",
                     hit==1?Gap3(at,point):-1.0,normal.x,normal.y,normal.z);}
            Call(pc,"SetControlRotation").Set("NewRotation",keep).Invoke();}}
    if(InterlockedExchange(&aim_test_,0)){Context c;
        if(ContextNow(c))if(Object* pc=PlayerController(c.pawn)){const RotatorD r{aim_test_pitch_,ActorYaw(c.pawn)+aim_test_yaw_,0};
            Logf("NPC_TEST aim pitch=%.0f yaw=%.0f ok=%d",r.pitch,r.yaw,Call(pc,"SetControlRotation").Set("NewRotation",r).Invoke()?1:0);}}
    if(const LONG ms=InterlockedExchange(&timeline_watch_ms_,0)){timeline_watch_until_=GetTickCount64()+static_cast<ULONGLONG>(ms);timeline_watch_last_.clear();}
    if(!timeline_watch_until_)return;const ULONGLONG now=GetTickCount64();
    if(now>=timeline_watch_until_){timeline_watch_until_=0;Logf("NPC_TEST timelinewatch off");return;}
    std::string text="-";Object* ability=nullptr;
    if(skill_.handle>0&&Same(skill_.asc)){bool instance=false;ability=SpecAbility(skill_.asc.object,skill_.handle,instance);if(!instance)ability=nullptr;}
    static Object* const task_class=Class("/Script/LegoAbility.LGATask_PlayTimeline");
    const int32_t time_at=task_class?ue::Offset(task_class,"CurrentTime"):-1,line_at=task_class?ue::Offset(task_class,"Timeline"):-1;
    const int32_t tasks_at=ue::Alive(ability)?Offset(ability,"ActiveTasks"):-1;
    if(ability&&task_class&&tasks_at>=0&&time_at>=0&&line_at>=0&&FaceReadable(ability+tasks_at,sizeof(Array))){
        text=skill_.name+":";const Array tasks=ue::At<Array>(ability,tasks_at);
        if(tasks.num>0&&tasks.num<=64&&FaceReadable(tasks.data,static_cast<size_t>(tasks.num)*sizeof(Object*)))
            for(int32_t i=0;i<tasks.num;++i){Object* t=static_cast<Object**>(tasks.data)[i];if(!ue::Alive(t))continue;
                if(ue::IsA(t,task_class))text+=" "+Text(ue::At<Object*>(t,line_at));else text+=" ("+Text(ue::ClassOf(t))+")";}}
    if(text==timeline_watch_last_)return;timeline_watch_last_=text;
    Context c;Vector at{};uint8_t mode=0;
    if(ContextNow(c)&&ActorAt(c.pawn,at)){Object* move=Property<Object*>(c.pawn,"CharacterMovement");if(ue::Alive(move)&&Offset(move,"MovementMode")>=0)mode=Property<uint8_t>(move,"MovementMode");}
    std::string times;
    if(ability&&tasks_at>=0){const Array tasks=ue::At<Array>(ability,tasks_at);
        if(tasks.num>0&&tasks.num<=64)for(int32_t i=0;i<tasks.num;++i){Object* t=static_cast<Object**>(tasks.data)[i];
            if(ue::Alive(t)&&task_class&&ue::IsA(t,task_class)){char v[64]{};_snprintf_s(v,sizeof(v),_TRUNCATE," %.3f",ue::At<float>(t,time_at));times+=v;}}}
    Logf("NPC_TEST timelinewatch %s (times%s) hero at (%.0f,%.0f,%.0f) mode=%u",text.c_str(),times.c_str(),at.x,at.y,at.z,mode);
}
// ---- 武器派生 ------------------------------------------------------------------------------------------
// Casts weapon node `index` (the weapon is in hand). The weapon goes back when the move ends (no native combo after it).
void CastWeaponNode(const Context& context,int index,int button,ULONGLONG now){
    const NpcCombo::Node* node=ComboNode(index);if(!node)return;
    HookAim aim;
    if(node->type==NpcCombo::NodeType::Hook){aim=PickHookAim(context.pawn,NpcCombo::ClampHookDist(node->hook_dist));hook_anchor_={};}
    if(node->type==NpcCombo::NodeType::Hook&&node->hook_auto&&aim.enemy){  // 自动: the target's guard picks the hook move now
        Object* target=aim.enemy;std::string which;const bool broken=GuardBroken(target,which);
        NpcCombo::ResolveHook(combo_.config.chain[static_cast<size_t>(index)],!broken);node=ComboNode(index);
        Logf("NPC_COMBO hook node=%d auto: target=%s guard %s (hit state %s) -> %s",index,Text(target).c_str(),broken?("broken ("+which+")").c_str():"not broken",
             HitStateOf(target).c_str(),broken?"pull it over (Drag)":"go to it (HookMove)");}
    const int keep=combo_.cursor;
    combo_.cursor=index;combo_.playing=index;combo_.button=button;combo_.phase=ComboRun::Phase::Starting;combo_.at=now;combo_.cut_at=0;
    StepCursor(keep);
    combo_.rated=nullptr;combo_.first_rated=false;combo_.rate_check=0;combo_.before=ActiveMontage(context);
    const ComboWeapons::Move* move=ComboWeapons::MoveOf(node->weapon,node->ability);
    combo_.native=false;
    const bool hook=node->type==NpcCombo::NodeType::Hook,anchor=hook&&!aim.enemy&&aim.anchor,miss=hook&&!aim.enemy&&!aim.anchor;
    if(hook)AimHook(context.pawn,index,anchor||node->hook_self,aim);
    if(anchor)StartHookAnchor(context.pawn,index,aim);  // the ground or a wall: the hero flies there (HookMove)
    if(miss)Logf("NPC_COMBO hook node=%d: nothing in reach (%s): the pull is thrown and comes back",index,aim.how.c_str());
    const std::string cast=anchor?std::string(NpcCombo::kHookGo):miss?std::string(NpcCombo::kHookPull):node->ability;
    hook_cast_node_=hook?index:-1;hook_cast_=hook?cast:std::string();
    hook_no_lock_=hook&&!aim.enemy;hook_no_lock_name_=hook_no_lock_?cast:std::string();
    if(hook&&!aim.enemy&&aim.locked_far)  // a lock out of reach would draw the game's hook to it
        if(Object* targeting=TargetingComponent(context.pawn))
            Logf("NPC_COMBO hook node=%d: the lock out of reach released ok=%d",index,ue::FindFunction(ue::ClassOf(targeting),"StopLockTarget")&&Call(targeting,"StopLockTarget").Invoke()?1:0);
    if(!CastHeroMove(context,cast,hook?L"钩子":L"武器派生",kChainWaitMs)){
        ResetCombo("grant-failed",hook?L"派生链停止：拿着追命锁镰时主角没有这个钩子技能":L"派生链停止：拿着这把武器时主角没有这个技能");return;}
    Logf("NPC_COMBO play node=%d button=%d trigger=%d weapon=%s skill=%s stage=%s",index,button,combo_.trigger,node->weapon.c_str(),node->ability.c_str(),
         move&&move->stage[0]?move->stage:"-");
    PublishCombo(hook?L"派生链播放中：钩子":L"派生链播放中：武器派生");
}
// The weapon for node `index`: cast at once when it is in hand, else taken first (no blink: the swap shows).
void StartWeaponNode(const Context& context,int index,int button,ULONGLONG now){
    const NpcCombo::Node* node=ComboNode(index);if(!node)return;
    const bool shaqi=ComboWeapons::IsShaqi(node->weapon);
    std::string current;
    const bool known=!shaqi&&WeaponPresets::CurrentMainWeaponId(current);
    const bool in_hand=weapon_.stage==WeaponHold::Stage::Holding?weapon_.weapon==node->weapon:(weapon_.stage==WeaponHold::Stage::Off&&known&&current==node->weapon);
    if(in_hand){Logf("NPC_COMBO weapon %s already in hand look=%s",node->weapon.c_str(),LookName(LookNow()));CastWeaponNode(context,index,button,now);return;}
    if(pending_.handle>0)DropPendingSkill("cut-by-weapon");
    if(skill_.handle>0)ClearSkill("cut-by-weapon");
    const int keep=combo_.cursor;
    combo_.cursor=index;combo_.playing=index;combo_.button=button;combo_.phase=ComboRun::Phase::Starting;combo_.at=now;combo_.cut_at=0;
    StepCursor(keep);
    weapon_.stage=WeaponHold::Stage::Taking;weapon_.node=index;weapon_.button=button;weapon_.weapon=node->weapon;weapon_.since=now;
    WeaponPresets::RequestTemporaryWeapon(node->weapon,shaqi);
    Logf("NPC_COMBO play node=%d weapon=%s skill=%s (taking the weapon directly, no blink; in hand %s, look=%s)",index,node->weapon.c_str(),node->ability.c_str(),
         known?current.c_str():"?",LookName(LookNow()));
    PublishCombo(L"派生链：换武器");
}
// The weapon from before back (after the move or the chain, or before a move that is no 武器派生: node, deferred).
void BeginWeaponReturn(const char* why,int node,int button){
    if(weapon_.stage==WeaponHold::Stage::Off)return;
    if(weapon_.stage==WeaponHold::Stage::Returning){if(node<0)weapon_.node=-1;return;}
    const WeaponPresets::TemporaryView held=WeaponPresets::GetTemporary();
    Logf("NPC_COMBO weapon return reason=%s weapon=%s changed=%d",why,weapon_.weapon.c_str(),held.changed?1:0);
    weapon_.stage=WeaponHold::Stage::Returning;weapon_.since=GetTickCount64();weapon_.node=node;weapon_.button=button;
    WeaponPresets::RequestTemporaryRestore();
}
void DriveWeapon(){
    if(weapon_.stage==WeaponHold::Stage::Off)return;
    const ULONGLONG now=GetTickCount64();
    const WeaponPresets::TemporaryView held=WeaponPresets::GetTemporary();
    if(weapon_.stage==WeaponHold::Stage::Taking){
        if(held.phase==WeaponPresets::TemporaryPhase::On&&held.weapon==weapon_.weapon){
            weapon_.stage=WeaponHold::Stage::Holding;
            Logf("NPC_COMBO weapon on %s after %llums",weapon_.weapon.c_str(),now-weapon_.since);
            Context context;const int node=weapon_.node;
            if(node<0||combo_.trigger<0||combo_.playing!=node||!ContextNow(context)||context.pawn!=ChainPawn().object){BeginWeaponReturn("chain-gone");return;}
            CastWeaponNode(context,node,weapon_.button,now);return;}
        if(held.phase==WeaponPresets::TemporaryPhase::Failed){
            char why[400]{};WideCharToMultiByte(CP_UTF8,0,held.status.c_str(),-1,why,static_cast<int>(sizeof(why)),nullptr,nullptr);
            Logf("NPC_COMBO weapon failed %s: %s",weapon_.weapon.c_str(),why);
            ResetCombo("weapon-failed",L"派生链停止：换武器失败（主角没有这把武器，或正忙）");return;}
        if(now>weapon_.since+kWeaponTakeMs){Logf("NPC_COMBO weapon timeout %s",weapon_.weapon.c_str());ResetCombo("weapon-timeout",L"派生链停止：换武器超时");}
        return;
    }
    if(weapon_.stage==WeaponHold::Stage::Returning){
        const int node=weapon_.node,button=weapon_.button;
        if(held.phase==WeaponPresets::TemporaryPhase::Off){
            Logf("NPC_COMBO weapon back after %llums",now-weapon_.since);weapon_={};
            if(node>=0&&combo_.trigger>=0&&combo_.playing==node){PlayNode(node,button,now);return;}
            if(borrow_.stage==Borrow::Stage::Wearing&&borrow_.row<0&&(combo_.trigger<0||!NpcCombo::Continues(combo_.config,combo_.cursor)))
                BeginReturn("weapon-back");  // then the look, unless the chain goes on (连续变身)
            return;}
        if(held.phase==WeaponPresets::TemporaryPhase::Failed||now>weapon_.since+kWeaponReturnMs){
            Logf("NPC_COMBO weapon could not come back (%s)",held.phase==WeaponPresets::TemporaryPhase::Failed?"refused":"timeout");weapon_={};
            Status(L"武器派生结束后没能换回原武器，请在装备菜单手动切换");
            if(node>=0&&combo_.trigger>=0)ResetCombo("weapon-return-failed",L"派生链停止：没能换回原武器");
            else if(borrow_.stage==Borrow::Stage::Wearing&&borrow_.row<0)BeginReturn("weapon-back");
            return;}
        return;
    }
    if(combo_.trigger<0)BeginWeaponReturn("chain-over");  // Holding: let go once the chain is over
}
// ---- BOSS 技能 look changes ------------------------------------------------------------------------------
// A chain's look change held back: what holds it (logged when it changes).
void ChainLookWaits(const char* gate){
    if(!gate||borrow_.gate==gate)return;
    borrow_.gate=gate;
    Logf("NPC_COMBO boss look waiting id=%s gate=%s (%llums)",LookName(waiting_entry_),gate,GetTickCount64()-borrow_.since);
}
void DriveBorrow(){
    if(borrow_.stage==Borrow::Stage::Off||borrow_.stage==Borrow::Stage::Wearing)return;
    const ULONGLONG now=GetTickCount64();
    if(borrow_.stage==Borrow::Stage::Going){
        if(LookSettled(borrow_.row)){
            borrow_.stage=Borrow::Stage::Wearing;
            Logf("NPC_COMBO boss look on id=%s after %llums",LookName(borrow_.row),now-borrow_.since);
            const NpcCombo::Node* node=combo_.trigger>=0&&combo_.playing==borrow_.node?ComboNode(borrow_.node):nullptr;
            Context context;
            if(!node||!ContextNow(context)||context.pawn!=ChainPawn().object){BeginReturn("chain-gone");return;}
            if(NpcCombo::IsLookChange(*node)){
                // 变身 / 变回主角: done; the next press goes on from here (a press during the change plays now).
                combo_.phase=ComboRun::Phase::Running;combo_.started=now;combo_.at=now;
                Logf("NPC_COMBO look node=%d on: %s (kept after the chain)",borrow_.node,LookName(borrow_.row));
                PublishCombo(L"派生链：已变身，等待下一次按键");return;}
            if(NpcCombo::CastsWeapon(*node)){StartWeaponNode(context,borrow_.node,borrow_.button,now);return;}
            if(node->type==NpcCombo::NodeType::Grab){
                // The grab's boss look is on: DriveCombo asks the grab module once the move is prepared in the chain's slot.
                combo_.at=now;combo_.grab_asked=false;LoadChainGrab(combo_.grab_variant,"boss look on");
                PublishCombo(L"派生链播放中：投技");return;}
            combo_.at=now;combo_.before=ActiveMontage(context);
            if(!CastMove(context,MovePackage(static_cast<size_t>(borrow_.row),node->ability),L"BOSS 技能",kChainWaitMs))
                ResetCombo("grant-failed",L"派生链停止：这一招无法授予主角");
            else PublishCombo(L"派生链播放中：BOSS 技能");
            return;}
        // A look that never went on is not kept (保留变身 / 变身): the chain ends where it would have before.
        if(waiting_action_<0&&now>borrow_.asked+250){Logf("NPC_COMBO boss look failed id=%s (now %s)",LookName(borrow_.row),LookName(LookNow()));
            if(combo_.home==borrow_.row)combo_.home=borrow_.home_before;
            ResetCombo("boss-look-failed",L"派生链停止：变身为该 Boss 没有完成");return;}
        if(now>borrow_.since+kBorrowLookMs){
            Logf("NPC_COMBO boss look timeout id=%s after %llums: waited on gate=%s",LookName(borrow_.row),now-borrow_.since,borrow_.gate.empty()?"(the change never came up)":borrow_.gate.c_str());
            if(combo_.home==borrow_.row)combo_.home=borrow_.home_before;
            ResetCombo("boss-look-timeout",L"派生链停止：变身被游戏状态挡住，没有变身");}
        return;
    }
    // Returning (to the owner's look, or to the one a 变身 / 变回主角 node set: borrow_.target)
    if(LookSettled(borrow_.target)){
        const int node=borrow_.node,button=borrow_.button;
        Logf("NPC_COMBO boss look back to %s after %llums%s",LookName(borrow_.target),now-borrow_.since,borrow_.target!=borrow_.owner?" (the chain's kept look: 变身 / 保留变身)":"");
        borrow_={};
        if(node>=0&&combo_.trigger>=0&&combo_.playing==node)PlayNode(node,button,now);
        return;}
    if(waiting_action_<0&&now>=borrow_.asked+kBorrowAskMs){
        if(borrow_.asks<kBorrowAsks)AskLook(borrow_.target,now);
        else{Logf("NPC_COMBO boss look could not return to %s after %u asks; left as it is",LookName(borrow_.target),borrow_.asks);
            Status(L"BOSS 技能结束后没能变回原外观，请在外观页手动切换");borrow_={};return;}}
    if(combo_.trigger>=0&&now>borrow_.since+kBorrowReturnMs){borrow_.node=-1;ResetCombo("boss-return-timeout",L"派生链停止：变回原外观超时");}
}
// ---- BOSS 技能 preloading ---------------------------------------------------------------------------------
// The looks and moves the active .combo set's BOSS 技能, 变身 and 投技 nodes use are loaded before they are needed and
// held (AddRef), so a look change or the move's first cast does not stall on loads: the look's blueprint (mesh,
// skeleton, materials and parts come with it), its body / skeleton / head, its weapon classes, the move's ability (its
// montages and bullet data come with it) and the blink effects. Once a look's blueprint is held its CDO and part
// templates are looked up too (WarmLook), so the look change finds them cached.
struct PreloadItem { std::string path; bool is_class=false; int32_t row=-1; std::string skill;
    enum class State { Waiting, Loading, Deferred, Held, Missing }; State state=State::Waiting; Identity pin; SoftPath soft; bool soft_ok=false;
    ULONGLONG asked=0; double game_ms=0; };
struct PreloadGroup { size_t begin=0,end=0; int32_t row=-1; std::string skill; bool reported=false,started=false; ULONGLONG t0=0; };
struct Preload { uint64_t generation=0; Object* world=nullptr; std::vector<PreloadItem> items; std::vector<PreloadGroup> groups; size_t poll=0; uint32_t uuid=0;
    bool complete=false,idle=false;
    int async=-1; // -1 not checked yet, 0 blocking loads, 1 the engine's async loader
    std::vector<int32_t> warm,display_rows; };  // looks whose part templates are found once their donor is held
Preload preload_;
constexpr size_t kPreloadInFlight=6,kPreloadAsksPerFrame=2,kPreloadPollsPerFrame=8;constexpr ULONGLONG kPreloadTimeoutMs=30000;
double QpcMs(LONGLONG from){
    static const double frequency=[]{LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return f.QuadPart>0?static_cast<double>(f.QuadPart):1.0;}();
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);return static_cast<double>(now.QuadPart-from)*1000.0/frequency;}
LONGLONG QpcNow(){LARGE_INTEGER now{};QueryPerformanceCounter(&now);return now.QuadPart;}
void ReleasePreloadPin(Identity& pin){
    if(pin.object&&ClassRefsReady()&&RawIdentity(pin,pin.index,pin.serial))reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base()+0x158fa30)(pin.object);
    pin={};
}
size_t HeldPreloads(){size_t held=0;for(const PreloadItem& i:preload_.items)held+=i.pin.object?1:0;return held;}
void ReleasePreload(){
    size_t released=0,total=0;
    for(PreloadItem& item:preload_.items)if(item.pin.object){++total;const bool live=ClassRefsReady()&&RawIdentity(item.pin,item.pin.index,item.pin.serial);ReleasePreloadPin(item.pin);released+=live?1:0;}
    if(total)Logf("NPC_COMBO preload released=%zu of %zu",released,total);
}
std::vector<PreloadItem> PreloadWanted(){
    std::vector<PreloadItem> items;std::set<std::pair<int32_t,std::string>> seen;std::set<std::pair<std::string,bool>> paths;
    const auto configs=NpcCombo::All();
    const MotianGrab::Snapshot grab=MotianGrab::GetSnapshot();
    preload_.display_rows.clear();
    // Prefer the current chain, then the hero's chain. Bare skills and assist
    // actors do not need an appearance bundle and must not occupy the pool.
    const std::string current=ComboProfile();
    for(int priority=0;priority<3;++priority)for(const auto& [profile,config]:configs){
        const int rank=profile==current?0:profile==NpcCombo::kHeroProfile?1:2;if(rank!=priority||!config.enabled)continue;
        for(size_t at=0;at<config.chain.size();++at){const auto& node=config.chain[at];int row=-1;
            const bool own=NpcCombo::TakesOwnLook(node)&&NpcCombo::OwnLookEverywhere(config,at);  // 不变身 in every chain reaching it
            if(node.type==NpcCombo::NodeType::Look||(node.type==NpcCombo::NodeType::BossSkill&&!own))row=ProfileRow(node.boss);
            else if(node.type==NpcCombo::NodeType::Grab&&!own){const auto* variant=GrabVariantData(grab,GrabVariantOf(node,grab));if(variant)row=SkinForGrab(variant->boss_id,variant->source);}
            if(row>=0&&std::find(preload_.display_rows.begin(),preload_.display_rows.end(),row)==preload_.display_rows.end())preload_.display_rows.push_back(row);
        }
    }
    const auto add=[&](const char* path,bool is_class,int32_t row,const std::string& skill){
        if(!path||!*path||!paths.emplace(path,is_class).second)return;items.push_back({path,is_class,row,skill});};
    for(const auto& [profile,config]:configs)for(const NpcCombo::Node& n:config.chain){
        // A BOSS 援护 calls in that boss's combat blueprint (its own mesh, weapons and the move): the same set.
        if(!NpcCombo::PicksBossMove(n)||!NpcCombo::IsMove(n))continue;
        const int32_t row=ProfileRow(n.boss);if(row<0)continue;
        if(!seen.emplace(row,n.ability).second)continue;
        const Profile& p=kProfiles[row];const NpcSkillSet& set=kNpcSkillSets[static_cast<size_t>(row)];
        add(p.donor,true,row,n.ability);add(p.body,false,row,n.ability);add(p.skeleton,false,row,n.ability);
        if(p.head&&std::strchr(p.head,'.'))add(p.head,false,row,n.ability);
        for(size_t w=0;w<set.weapon_count;++w)add(set.weapons[w].actor_class,true,row,n.ability);
        // The move's own weapon set when it switches the boss's Main weapon (npc_weapon_sets.generated.h).
        if(const size_t pick=MovePick(static_cast<size_t>(row),n.ability);pick!=SIZE_MAX){const NpcWeaponSets& sets=kNpcWeaponSetsBySkin[static_cast<size_t>(row)];
            if(pick<sets.skill_count&&sets.start[pick]>=0&&static_cast<size_t>(sets.start[pick])<sets.entry_count){const NpcWeaponEntry& e=sets.entries[sets.start[pick]];
                for(size_t w=0;w<e.count;++w)add(e.weapons[w].actor_class,true,row,n.ability);}}
        // The look's combat blueprint (its capsule, boss weapons and skill classes: LoadCombatBlueprint at the look's
        // apply). Not loaded before, the first change into the look loaded it on the game thread (1172 ms for PengMeng).
        add(set.monster,true,row,n.ability);
        // The move last: a group ends at its own ability (its log line).
        const std::string ga=MovePackage(static_cast<size_t>(row),n.ability);
        if(!ga.empty())add(ga.c_str(),true,row,n.ability);
    }
    // An NPC look's own chain (its profile): its moves are that boss's skills, cast while the look is on: after a
    // 保留变身 / 变身 the hero's chain ended in (the look's chain takes over), or with the look put on from its page.
    // The look is on (loaded) by then, so only the moves: their abilities (montages and bullet data come with them);
    // a 闪避 node, every dodge of the look (the stick picks one at the press).
    for(const auto& [profile,config]:configs){
        const int32_t row=ProfileRow(profile);if(row<0||!config.enabled)continue;
        for(const NpcCombo::Node& n:config.chain){
            if(n.type==NpcCombo::NodeType::Skill&&!n.ability.empty()){
                if(!seen.emplace(row,n.ability).second)continue;
                const std::string ga=MovePackage(static_cast<size_t>(row),n.ability);
                if(!ga.empty())add(ga.c_str(),true,row,n.ability);
            }else if(n.type==NpcCombo::NodeType::AutoDodge){
                if(!seen.emplace(row,"#dodges").second)continue;
                const NpcDodgeSet& dodges=kNpcDodgeSets[static_cast<size_t>(row)];
                for(size_t d=0;d<dodges.count;++d)add(dodges.dodges[d].ability,true,row,"dodges");
            }
        }
    }
    // 变身 nodes: their look (no move).
    for(const auto& [profile,config]:configs)for(const NpcCombo::Node& n:config.chain){
        if(n.type!=NpcCombo::NodeType::Look||n.boss.empty())continue;
        const int32_t row=ProfileRow(n.boss);if(row<0)continue;
        if(!seen.emplace(row,"#look").second)continue;
        const Profile& p=kProfiles[row];const NpcSkillSet& set=kNpcSkillSets[static_cast<size_t>(row)];const std::string key="look";
        add(p.donor,true,row,key);add(p.body,false,row,key);add(p.skeleton,false,row,key);
        if(p.head&&std::strchr(p.head,'.'))add(p.head,false,row,key);
        for(size_t w=0;w<set.weapon_count;++w)add(set.weapons[w].actor_class,true,row,key);
        add(set.monster,true,row,key);  // its combat blueprint, as for BOSS 技能
    }
    // 投技 nodes: the look of the grab's boss (the hero takes it for the grab; the grab's own assets are the grab
    // module's: its slot for the chain).

    for(const auto& [profile,config]:configs)for(const NpcCombo::Node& n:config.chain){
        if(n.type!=NpcCombo::NodeType::Grab)continue;
        const MotianGrab::Variant* v=GrabVariantData(grab,GrabVariantOf(n,grab));if(!v)continue;
        const int32_t row=SkinForGrab(v->boss_id,v->source);if(row<0)continue;
        if(!seen.emplace(row,"#grab").second)continue;
        const Profile& p=kProfiles[row];const NpcSkillSet& set=kNpcSkillSets[static_cast<size_t>(row)];const std::string key="grab:"+n.ability;
        add(p.donor,true,row,key);add(p.body,false,row,key);add(p.skeleton,false,row,key);
        if(p.head&&std::strchr(p.head,'.'))add(p.head,false,row,key);
        for(size_t w=0;w<set.weapon_count;++w)add(set.weapons[w].actor_class,true,row,key);
        add(set.monster,true,row,key);  // its combat blueprint, as for BOSS 技能
    }
    if(!items.empty())add("/HuaShira/Characters/Hero/Hero_Hun/Hun_CopyPose",true,-1,"copy-pose");
    if(!items.empty())for(const char* fx:{kBlinkOutFx,kBlinkInFx,kBlinkOutSound,kBlinkInSound})add(fx,false,-1,"");
    return items;
}
Object* PreloadOne(const PreloadItem& item){
    if(!item.is_class)return LoadAssetPath(item.path.c_str());
    std::string package=item.path;const size_t dot=package.find('.');if(dot!=std::string::npos)package.resize(dot);  // "/Pkg/BP_X.BP_X_C" -> "/Pkg/BP_X"
    return LoadClassPackage(package);
}
// The soft reference of an item: an asset "/Pkg/Name.Name"; a class by its package ("/Pkg/BP_X" or "/Pkg/BP_X.BP_X_C") as BP_X_C.
bool PreloadSoft(PreloadItem& item){
    if(item.soft_ok)return true;
    std::string package=item.path,asset;const size_t dot=package.find('.');
    if(item.is_class){if(dot!=std::string::npos)package.resize(dot);asset=package.substr(package.find_last_of('/')+1)+"_C";}
    else{if(dot==std::string::npos)return false;asset=package.substr(dot+1);package.resize(dot);}
    const std::wstring p(package.begin(),package.end()),a(asset.begin(),asset.end());
    item.soft=SoftPath{};item.soft.package=ue::MakeName(p.c_str());item.soft.asset=ue::MakeName(a.c_str());
    item.soft_ok=!item.soft.package.None()&&!item.soft.asset.None();return item.soft_ok;
}
// The engine's async loader: the asset manager (KismetSystemLibrary.LoadAsset needs it) and the functions, with
// FLatentActionInfo as this build lays it out.
int32_t latent_size_=0;
bool PreloadAsyncReady(){
    if(preload_.async>=0)return preload_.async==1;
    if(!ue::Alive(engine_))engine_=ue::FindEngine();
    Object* manager=ue::Alive(engine_)?Property<Object*>(engine_,"AssetManager"):nullptr;
    Object* library=Kismet();Object* type=library?ue::ClassOf(library):nullptr;
    Object* load=type?ue::FindFunction(type,"LoadAsset"):nullptr,*load_class=type?ue::FindFunction(type,"LoadAssetClass"):nullptr;
    const bool functions=load&&load_class&&type&&ue::FindFunction(type,"Conv_SoftObjectReferenceToObject")&&ue::FindFunction(type,"Conv_SoftClassReferenceToClass")&&
        ue::Offset(load,"Asset")>=0&&ue::Offset(load_class,"AssetClass")>=0&&ue::Offset(load,"WorldContextObject")>=0&&ue::Offset(load,"LatentInfo")>=0;
    Object* latent=Class("/Script/Engine.LatentActionInfo");
    const bool layout=latent&&ue::StructSize(latent)==24&&ue::Offset(latent,"Linkage")==0&&ue::Offset(latent,"UUID")==4&&ue::Offset(latent,"ExecutionFunction")==8&&ue::Offset(latent,"CallbackTarget")==16;
    latent_size_=layout?24:0;
    preload_.async=ue::Alive(manager)&&functions&&layout?1:0;
    Logf("NPC_COMBO preload loader=%s (asset manager %s, LoadAsset/LoadAssetClass %s, LatentActionInfo %s)",preload_.async?"async":"blocking one per idle upkeep",
         manager?Text(manager).c_str():"none",functions?"found":"missing",layout?"24 bytes":"unexpected");
    return preload_.async==1;
}
// LoadAsset / LoadAssetClass with the world as the latent action's target (a unique UUID, no resume function,
// Linkage -1: nothing is called back); OnLoaded stays unbound. The load runs in the engine's streamable manager.
bool PreloadAsk(PreloadItem& item,Object* world){
    if(!PreloadSoft(item)||latent_size_!=24||!ue::Alive(world))return false;
    struct LatentInfo { int32_t linkage=-1,uuid=0;Name function;Object* target=nullptr; };static_assert(sizeof(LatentInfo)==24);
    LatentInfo info;info.uuid=static_cast<int32_t>(0x50425A00u+(++preload_.uuid&0xffffu));info.target=world;
    Call load(Kismet(),item.is_class?"LoadAssetClass":"LoadAsset");
    return load.Set("WorldContextObject",world).Set(item.is_class?"AssetClass":"Asset",item.soft).Set("LatentInfo",info).Invoke();
}
// Loaded: in the engine's object hash with none of RF_NeedLoad, RF_NeedPostLoad, RF_NeedPostLoadSubobjects, RF_WillBeLoaded.
Object* PreloadResolved(PreloadItem& item){
    if(!PreloadSoft(item))return nullptr;
    Call get(Kismet(),item.is_class?"Conv_SoftClassReferenceToClass":"Conv_SoftObjectReferenceToObject");
    if(!get.Set(item.is_class?"SoftClass":"SoftObject",item.soft).Invoke())return nullptr;
    Object* object=get.Get<Object*>("ReturnValue");
    if(!ue::Alive(object))return nullptr;
    return (ue::At<uint32_t>(object,static_cast<int32_t>(ue::layout::kObjFlags))&(0x400u|0x1000u|0x2000u|0x8000000u))?nullptr:object;
}
// Held: a package-root asset gets the counted strong reference (AddRef), released when the set drops it.
void PreloadHold(PreloadItem& item,Object* asset){
    item.state=asset?PreloadItem::State::Held:PreloadItem::State::Missing;
    if(asset&&ClassRefsReady()){Object* outer=ue::OuterOf(asset);
        if(ue::Alive(outer)&&ue::IsA(outer,Class("/Script/CoreUObject.Package"))&&!ue::OuterOf(outer)){const Identity id=Id(asset);
            if(Same(id)){reinterpret_cast<void(__fastcall*)(Object*)>(ue::Base()+0x1573ff0)(asset);item.pin=id;}}}
    if(!asset)Logf("NPC_COMBO preload missing %s",item.path.c_str());
    if(asset&&item.is_class&&item.row>=0&&static_cast<size_t>(item.row)<std::size(kProfiles)&&item.path==kProfiles[item.row].donor&&
       std::find(preload_.warm.begin(),preload_.warm.end(),item.row)==preload_.warm.end())preload_.warm.push_back(item.row);
}
void BuildPreloadGroups(){
    preload_.groups.clear();
    for(size_t i=0;i<preload_.items.size();++i){
        const PreloadItem& item=preload_.items[i];
        if(preload_.groups.empty()||preload_.groups.back().row!=item.row||preload_.groups.back().skill!=item.skill){PreloadGroup g;g.begin=i;g.row=item.row;g.skill=item.skill;preload_.groups.push_back(g);}
        preload_.groups.back().end=i+1;}
    for(PreloadGroup& g:preload_.groups){bool done=true;for(size_t i=g.begin;i<g.end;++i)done=done&&preload_.items[i].state==PreloadItem::State::Held;g.reported=done;}  // nothing new: no line
}
// A look whose donor is held: its CDO and part templates are found now (DefaultObject, PartTemplates), so the look
// change itself finds them cached. One look per call.
void WarmLook(){
    if(preload_.warm.empty()||!preload_.idle)return;
    const int32_t row=preload_.warm.front();preload_.warm.erase(preload_.warm.begin());
    const LONGLONG t0=QpcNow();const Profile& profile=kProfiles[row];
    Object* cls=Class((std::string(profile.donor)+"."+std::string(profile.donor).substr(std::string(profile.donor).find_last_of('/')+1)+"_C").c_str());
    Object* cdo=cls?DefaultObject(cls):nullptr;std::vector<Object*> templates;std::vector<int32_t> depths;size_t found=0;
    if(cdo){PartTemplates(cls,cdo,profile,templates,depths);for(Object* o:templates)found+=o?1:0;}
    Logf("NPC_COMBO preload warmed look=%s cdo=%d parts=%zu/%zu gameThreadMs=%.1f",LookName(row),cdo?1:0,found,profile.part_count,QpcMs(t0));
}
void ReportPreloadGroups(){
    for(PreloadGroup& g:preload_.groups){
        if(g.reported)continue;size_t held=0,missing=0;double game=0;bool done=true;
        for(size_t i=g.begin;i<g.end;++i){const PreloadItem& item=preload_.items[i];game+=item.game_ms;
            if(item.state==PreloadItem::State::Held)++held;else if(item.state==PreloadItem::State::Missing)++missing;else done=false;}
        if(!done)continue;g.reported=true;
        const unsigned long long wall=g.t0?GetTickCount64()-g.t0:0;
        if(g.row>=0)Logf("NPC_COMBO preload boss=%s skill=%s ok=%d async done wallMs=%llu gameThreadMs=%.1f held=%zu missing=%zu pins=%zu",LookName(g.row),g.skill.c_str(),missing?0:1,
                         wall,game,held,missing,HeldPreloads());
        else Logf("NPC_COMBO preload blink effects ok=%d async done wallMs=%llu gameThreadMs=%.1f",missing?0:1,wall,game);
    }
}
void PreloadStep(bool upkeep);
void DrivePreload(const Context* context){
    Object* world=context?context->world:nullptr;
    const uint64_t generation=NpcCombo::Generation();
    if(generation!=preload_.generation||world!=preload_.world){
        preload_.complete=false;preload_.idle=false;
        std::vector<PreloadItem> want=PreloadWanted();
        const bool scene=world!=preload_.world;size_t kept=0,released=0,added=0;
        std::map<std::pair<std::string,bool>,size_t> lookup;
        for(size_t i=0;i<want.size();++i)lookup.emplace(std::make_pair(want[i].path,want[i].is_class),i);
        if(scene){ReleasePreload();preload_.warm.clear();pool_attempts_.clear();}
        else for(PreloadItem& old:preload_.items){
            const auto found=lookup.find({old.path,old.is_class});auto it=found!=lookup.end()?want.begin()+static_cast<std::ptrdiff_t>(found->second):want.end();
            if(it!=want.end()&&old.state!=PreloadItem::State::Missing){it->state=old.state;it->pin=old.pin;it->soft=old.soft;it->soft_ok=old.soft_ok;it->asked=old.asked;it->game_ms=0;old.pin={};++kept;}
            else if(old.pin.object){ReleasePreloadPin(old.pin);++released;}}
        for(const PreloadItem& w:want)added+=w.state==PreloadItem::State::Waiting?1:0;
        const bool changed=scene||kept!=preload_.items.size()||added||want.size()!=preload_.items.size();
        preload_.items=std::move(want);preload_.generation=generation;preload_.world=world;BuildPreloadGroups();
        if(changed&&(!preload_.items.empty()||released))
            Logf("NPC_COMBO preload set items=%zu kept=%zu new=%zu released=%zu (%s)",preload_.items.size(),kept,added,released,scene?"scene change: all loaded again":"set changed: only new items load");
    }
    if(!world)return;
    preload_.idle=context&&combo_.playing<0&&!skill_.asc.object&&Idle(*context);
    WarmLook();
    PreloadStep(true);
    std::vector<int32_t> wanted;
    for(int row:preload_.display_rows)if(std::any_of(preload_.items.begin(),preload_.items.end(),[&](const PreloadItem& item){return item.row==row&&item.state==PreloadItem::State::Held&&item.path==kProfiles[row].donor;}))wanted.push_back(row);
    if(!InterlockedCompareExchange(&experimental_,0,0)){PoolClear();return;}
    if(context){
        display_pool_.Prune([&](const DisplayBundle& b){return !b.broken&&Same(b.pawn)&&Same(b.source)&&Same(b.world)&&b.pawn.object==context->pawn&&b.source.object==context->mesh&&b.world.object==context->world;},PoolDrop);
        if(preload_.idle&&waiting_action_<0&&!cutscene_host_)WarmDisplayPool(*context,wanted);
    }else PoolClear();
}
// Asks and polls: every frame with the async loader, with the 250 ms upkeep (DrivePreload) when loads block.
void PreloadStep(bool upkeep){
    Object* world=preload_.world;
    if(preload_.complete||preload_.items.empty()||!ue::Alive(world))return;
    const bool async=PreloadAsyncReady();
    const bool blocking=upkeep&&preload_.idle&&combo_.playing<0&&!skill_.asc.object;
    if(!async&&!blocking)return;
    const LONGLONG step_start=QpcNow();bool blocked=false;
    const ULONGLONG now=GetTickCount64();
    size_t loading=0;for(const PreloadItem& i:preload_.items)loading+=i.state==PreloadItem::State::Loading?1:0;
    // Ask: a few new items a frame, a few in flight (async); one blocking load a frame otherwise.
    size_t asks=0;
    for(size_t g=0;g<preload_.groups.size();++g){PreloadGroup& group=preload_.groups[g];
        for(size_t i=group.begin;i<group.end;++i){PreloadItem& item=preload_.items[i];
            const bool deferred=item.state==PreloadItem::State::Deferred;
            if(item.state!=PreloadItem::State::Waiting&&!deferred)continue;
            if(deferred&&!blocking)continue;
            if(blocked||QpcMs(step_start)>=2.0)break;
            if(async?(asks>=kPreloadAsksPerFrame||loading>=kPreloadInFlight):asks>=1)break;
            const LONGLONG t0=QpcNow();
            if(!group.t0){group.t0=now;if(!group.started){group.started=true;
                if(group.row>=0)Logf("NPC_COMBO preload boss=%s skill=%s async start items=%zu (%s)",LookName(group.row),group.skill.c_str(),group.end-group.begin,async?"async":"blocking");
                else Logf("NPC_COMBO preload blink effects async start items=%zu (%s)",group.end-group.begin,async?"async":"blocking");}}
            Object* ready=PreloadResolved(item);  // loaded already (the game, or a set before): held at once
            if(ready)PreloadHold(item,ready);
            else if(async&&!deferred&&PreloadAsk(item,world)){item.state=PreloadItem::State::Loading;item.asked=now;++loading;}
            else if(blocking){PreloadHold(item,PreloadOne(item));blocked=true;}
            else item.state=PreloadItem::State::Deferred;  // never synchronously load on the per-frame combat path
            item.game_ms+=QpcMs(t0);++asks;}
        if(blocked||QpcMs(step_start)>=2.0||(async?(asks>=kPreloadAsksPerFrame||loading>=kPreloadInFlight):asks>=1))break;}
    // Poll the items in flight by their soft reference, a few a frame.
    size_t polls=0;
    for(size_t n=0;n<preload_.items.size()&&polls<kPreloadPollsPerFrame&&QpcMs(step_start)<2.0;++n){
        preload_.poll=(preload_.poll+1)%preload_.items.size();PreloadItem& item=preload_.items[preload_.poll];
        if(item.state!=PreloadItem::State::Loading)continue;++polls;
        const LONGLONG t0=QpcNow();Object* ready=PreloadResolved(item);
        if(ready)PreloadHold(item,ready);
        else if(now>item.asked+kPreloadTimeoutMs){Logf("NPC_COMBO preload %s: nothing after %llums async, deferred until idle",item.path.c_str(),now-item.asked);item.state=PreloadItem::State::Deferred;}
        item.game_ms+=QpcMs(t0);}
    ReportPreloadGroups();
    preload_.complete=std::all_of(preload_.items.begin(),preload_.items.end(),[](const PreloadItem& item){return item.state==PreloadItem::State::Held||item.state==PreloadItem::State::Missing;});
}
// ---- 慢放 on the hero's side ----------------------------------------------------------------------------------------
// The game's slomo moves are Lego timeline actions derived from /MagCombatAbilities/Actions/GAAction_Slomo. Its Slomo
// function gives a target a curve entry in its slomo map through SlomoTarget.Slomo (the action's Slomo ID, Multiplier,
// Curve, Use Slomo Duration) and keeps the handle in the action's Handles (TMap<target, SlomoHandleList>); its end (Stop
// Slomo Handles) stops every handle there. Each subclass picks the target: GAAction_PlayerSlomo (梦华圣母's time stops)
// GetPlayerCharacter, the base its caster, GAA_ExecutionSlomo the execution's target, GAAction_GlobalSlomo a world
// subsystem, GAAction_AreaSlomo the actors around. Cast by a boss against the player, the player slows; cast on the
// hero's side, the hero slowed (alone, or with its grab's victim).
// DriveSlomo: an action of an NPC move (/ActorData/Mobs/) cast by the hero, by an assist on its side, by a victim
// playing the receiving part (GEd / BEd / BDEd) of the hero's NPC grab, or more than kSlomoReachCm away from the hero
// (the move preview's stage), whose handles sit on the hero, the hero's grab partners or a world subsystem only: the
// action's own Slomo runs on every other character within kSlomoReachCm of the caster (not the caster, the grab pair or
// frozen ones; the map keeps their handles, so the action's own end stops them), then the handles on the hero, the pair
// and the subsystem are stopped (SlomoTarget.StopSlomo) and taken off their lists. A boss's own slow on the player, the
// hero's own moves and a slow that already holds other characters (an area) stay as the game made them.
constexpr double kSlomoReachCm=10000;   // 100 m around the caster: the fight (the move preview's stage is 800 m up)
constexpr ULONGLONG kSlomoScanMs=150,kSlomoClassMs=2000;
constexpr const char* kSlomoClasses[]={
    "/MagCombatAbilities/Actions/GAAction_PlayerSlomo.GAAction_PlayerSlomo_C","/MagCombatAbilities/Actions/GAAction_Slomo.GAAction_Slomo_C",
    "/MagCombatAbilities/Actions/GAAction_GlobalSlomo.GAAction_GlobalSlomo_C","/MagCombatAbilities/Actions/GAAction_AreaSlomo.GAAction_AreaSlomo_C",
    "/SGameExecution/Actions/GAA_ExecutionSlomo.GAA_ExecutionSlomo_C"};
struct SlomoEntry { Object* key=nullptr; Array* list=nullptr; };
struct SlomoMoved { Identity action; std::vector<Object*> ours; };   // the characters DriveSlomo gave this action's slow
struct SlomoSeen { Identity action; int32_t handle=0; };             // an action + its newest handle already decided
Object* slomo_classes_[std::size(kSlomoClasses)]{};
ULONGLONG slomo_class_at_=0,slomo_scan_at_=0;int slomo_scans_logged_=0;
std::vector<SlomoMoved> slomo_moved_;std::vector<SlomoSeen> slomo_seen_;std::vector<Object*> slomo_logged_functions_;
// Two readbacks after a move (+300 ms, +1 s): the hero's and each slowed character's CustomTimeDilation.
struct SlomoCheck { ULONGLONG at=0,last=0; std::vector<Identity> targets; std::string move; };SlomoCheck slomo_check_;
std::vector<Identity> slomo_cache_;ULONGLONG slomo_full_at_=0;float slomo_last_dilation_=1;  // the slomo actions of the last full scan
// The action's Handles read in place (the FScriptMap layout FindMainSlot reads: rows, allocation bits inline at +16 or
// at the pointer at +32, NumBits at +40; the map property's value offset at 0x80 and row size at 0x8c). False when
// anything is off; the lists point into the map (valid until the map changes).
bool SlomoEntries(Object* action,std::vector<SlomoEntry>& out,const char** why){
    out.clear();
    const auto prop=[](uintptr_t f){return reinterpret_cast<Object*>(f);};
    const uintptr_t map=Field(ue::ClassOf(action),"Handles");
    if(!map||FieldKind(prop(map))!=0x400000000000ull){*why="no Handles map";return false;}
    Object* key=ue::At<Object*>(prop(map),0x70),*value=ue::At<Object*>(prop(map),0x78);
    if(!key||!value||ue::At<int32_t>(key,ue::layout::kPropElementSize)!=8||FieldKind(value)!=0x100000){*why="Handles is no map of objects to structs";return false;}
    ue::Field fields[4];
    if(ue::Fields(ue::At<Object*>(value,0x70),fields,std::size(fields))!=1||fields[0].size!=static_cast<int32_t>(sizeof(Array))){*why="SlomoHandleList is no single array";return false;}
    const int32_t value_offset=ue::At<int32_t>(prop(map),0x80),stride=ue::At<int32_t>(prop(map),0x8c),list_at=fields[0].offset;
    unsigned char* at=action+ue::At<int32_t>(prop(map),ue::layout::kPropOffset);
    const int32_t count=*reinterpret_cast<const int32_t*>(at+8),bits=*reinterpret_cast<const int32_t*>(at+40);
    if(count==0)return true;
    unsigned char* data=*reinterpret_cast<unsigned char* const*>(at);const uint32_t* allocation=*reinterpret_cast<uint32_t* const*>(at+32);
    if(!allocation)allocation=reinterpret_cast<const uint32_t*>(at+16);
    if(stride<16||stride>256||value_offset<8||value_offset+list_at+static_cast<int32_t>(sizeof(Array))>stride||count<0||count>256||bits<count||
       (allocation==reinterpret_cast<const uint32_t*>(at+16)&&count>128)||
       !FaceReadable(data,size_t(count)*size_t(stride))||!FaceReadable(allocation,(size_t(count)+31)/32*4)){*why="Handles layout";return false;}
    for(int32_t i=0;i<count;++i){
        if(!(allocation[i/32]&(1u<<(i%32))))continue;
        unsigned char* row=data+size_t(i)*size_t(stride);Array* list=reinterpret_cast<Array*>(row+value_offset+list_at);
        if(list->num<0||list->num>256||list->max<list->num||(list->num&&!FaceReadable(list->data,size_t(list->num)*sizeof(int32_t)))){*why="Handles list";return false;}
        out.push_back({*reinterpret_cast<Object**>(row),list});
    }
    return true;
}
std::string ClassPackage(Object* object){Object* cls=ue::Alive(object)?ue::ClassOf(object):nullptr;Object* package=cls?ue::OuterOf(cls):nullptr;return package?Text(package):"";}
bool NpcMoveClass(Object* ability){return ClassPackage(ability).find("/ActorData/Mobs/")!=std::string::npos;}
// The receiving part of a paired move (a grab's victim side): GEd / BEd / BDEd in the ability's class name.
bool ReceivingMove(Object* ability){
    if(!ue::Alive(ability))return false;const std::string name=Text(ue::ClassOf(ability));
    return name.find("GEd")!=std::string::npos||name.find("BEd")!=std::string::npos||name.find("BDEd")!=std::string::npos;
}
// Whether one of the abilities the pawn's ASC runs now (ActivatableAbilities rows with an ActiveCount) passes the test.
template<class Test> bool RunsMove(Object* pawn,Test test){
    Object* asc=ue::Alive(pawn)?PawnAsc(pawn):nullptr;if(!ue::Alive(asc))return false;
    Object* type=Class("/Script/GameplayAbilities.GameplayAbilitySpec"),*container=Class("/Script/GameplayAbilities.GameplayAbilitySpecContainer");
    const int32_t specs=Offset(asc,"ActivatableAbilities"),items=container?ue::Offset(container,"Items"):-1,size=type?ue::StructSize(type):0,
        ability_at=type?ue::Offset(type,"Ability"):-1,count_at=type?ue::Offset(type,"ActiveCount"):-1;
    if(specs<0||items<0||size<=0||ability_at<0||count_at<0)return false;
    const Array array=ue::At<Array>(asc,specs+items);
    if(array.num<=0||array.num>4096||array.max<array.num||!FaceReadable(array.data,static_cast<size_t>(array.num)*static_cast<size_t>(size)))return false;
    for(int32_t i=0;i<array.num;++i){Object* row=static_cast<Object*>(array.data)+static_cast<size_t>(i)*static_cast<size_t>(size);
        if(ue::At<uint8_t>(row,count_at)&&test(ue::At<Object*>(row,ability_at)))return true;}
    return false;
}
// 0 friendly, 1 neutral, 2 hostile (SGameAIController teams), 255 unread.
uint8_t TeamAttitude(Object* a,Object* b){
    Object* teams=ue::Default("/Script/SGameAI.Default__SGameAIController");Call own(teams,"GetTeam"),other(teams,"GetTeam");
    if(!own.Set("Actor",a).Invoke()||!other.Set("Actor",b).Invoke())return 255;
    Call attitude(teams,"GetDefaultTeamAttitude");
    if(!attitude.Set("Team1",other.Get<uint8_t>("ReturnValue")).Set("Team2",own.Get<uint8_t>("ReturnValue")).Invoke())return 255;
    return attitude.Get<uint8_t>("ReturnValue");
}
Vector SlomoActorAt(Object* actor){Call where(actor,"K2_GetActorLocation");return where.Invoke()?where.Get<Vector>("ReturnValue"):Vector{};}
double SlomoDistance(const Vector& a,const Vector& b){const double x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z);}
// A SlomoTarget function on an object that has one (a Handles key passed the action's cast): a Blueprint override on
// its class first, else the interface's.
Object* SlomoTargetFunction(Object* target,const char* name){
    if(Object* own=ue::FindFunction(ue::ClassOf(target),name))return own;
    Object* iface=SlomoTargetClass();return iface?ue::FindFunction(iface,name):nullptr;
}
bool ImplementsSlomoTarget(Object* object){
    Object* iface=SlomoTargetClass();if(!iface)return false;
    Call implements(ue::Default("/Script/Engine.Default__KismetSystemLibrary"),"DoesImplementInterface");
    return implements.Set("TestObject",object).Set("Interface",iface).Invoke()&&implements.Get<bool>("ReturnValue");
}
// The action's length on its timeline (LGATimeline.Actions: the row whose Action is this action); 0 when unread.
double SlomoActionLength(Object* action){
    Object* timeline=ue::OuterOf(action);if(!ue::Alive(timeline))return 0;
    const uintptr_t actions=Field(ue::ClassOf(timeline),"Actions");
    if(!actions||FieldKind(reinterpret_cast<Object*>(actions))!=0x200000)return 0;
    Object* inner=ue::At<Object*>(reinterpret_cast<Object*>(actions),0x78);if(!inner||FieldKind(inner)!=0x100000)return 0;
    Object* type=ue::At<Object*>(inner,0x70);const int32_t size=ue::StructSize(type),action_at=ue::Offset(type,"Action");
    const uintptr_t length=Field(type,"duration");
    const int32_t length_size=length?ue::At<int32_t>(reinterpret_cast<void*>(length),ue::layout::kPropElementSize):0,length_at=length?ue::At<int32_t>(reinterpret_cast<void*>(length),ue::layout::kPropOffset):-1;
    const Array rows=ue::At<Array>(timeline,ue::At<int32_t>(reinterpret_cast<Object*>(actions),ue::layout::kPropOffset));
    if(size<=0||action_at<0||length_at<0||(length_size!=4&&length_size!=8)||rows.num<=0||rows.num>512||!FaceReadable(rows.data,size_t(rows.num)*size_t(size)))return 0;
    for(int32_t i=0;i<rows.num;++i){unsigned char* row=static_cast<unsigned char*>(rows.data)+size_t(i)*size_t(size);
        if(ue::At<Object*>(row,action_at)==action)return length_size==4?double(ue::At<float>(row,length_at)):ue::At<double>(row,length_at);}
    return 0;
}
// The action's own Slomo on one target: its object parameter takes the target, Total Duration the action's length,
// Elapsed Time stays 0 (the move has just reached the action). The parameters are those inside ParmsSize (a Blueprint
// function's locals follow them); logged once per function.
bool ActionSlomo(Object* action,Object* function,Object* target,double total,std::string& shape){
    ue::Field fields[24];const size_t n=ue::Fields(function,fields,std::size(fields));
    const int32_t parms=ue::At<uint16_t>(function,ue::layout::kFuncParmsSize);
    const auto has=[](const char* name,const char* part){std::string a=name,b=part;for(char& c:a)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return a.find(b)!=std::string::npos;};
    int aim=-1,total_at=-1,objects=0;shape.clear();
    for(size_t i=0;i<n;++i){const ue::Field& f=fields[i];if(f.offset+f.size>parms)continue;
        shape+=std::string(shape.empty()?"":", ")+f.name+"("+std::to_string(f.size)+")";
        if(std::strcmp(f.name,"ReturnValue")==0)continue;
        if(has(f.name,"total"))total_at=static_cast<int>(i);
        else if(has(f.name,"elapsed")||has(f.name,"id"))continue;
        else if(f.size==8){++objects;if(aim<0||has(f.name,"target")||has(f.name,"object")||has(f.name,"actor"))aim=static_cast<int>(i);}
    }
    if(aim<0)return false;
    Call call(action,function);
    unsigned char* slot=call.Slot(fields[aim].name,8);if(!slot)return false;std::memcpy(slot,&target,8);
    if(total_at>=0){const ue::Field& f=fields[total_at];
        if(unsigned char* t=call.Slot(f.name,static_cast<size_t>(f.size))){if(f.size==4){const float v=static_cast<float>(total);std::memcpy(t,&v,4);}else if(f.size==8)std::memcpy(t,&total,8);}}
    return call.Invoke();
}
bool SlomoSeenBefore(Object* action,int32_t handle){
    for(const SlomoSeen& s:slomo_seen_)if(s.action.object==action&&s.handle==handle&&Same(s.action))return true;
    if(slomo_seen_.size()>=64)slomo_seen_.erase(slomo_seen_.begin());
    slomo_seen_.push_back({Id(action),handle});return false;
}
void ConsiderSlomo(Object* action,Object* hero){
    std::vector<SlomoEntry> entries;const char* why="";
    if(!SlomoEntries(action,entries,&why)){
        if(std::find(slomo_logged_functions_.begin(),slomo_logged_functions_.end(),ue::ClassOf(action))==slomo_logged_functions_.end()){
            slomo_logged_functions_.push_back(ue::ClassOf(action));Logf("SLOMO_SIDE %s: Handles unread (%s)",Text(ue::ClassOf(action)).c_str(),why);}
        return;
    }
    Object* actor_class=Class("/Script/Engine.Actor"),*pawn_class=Class("/Script/Engine.Pawn"),*component_class=Class("/Script/Engine.ActorComponent");
    const auto world_key=[&](Object* key){return !(actor_class&&ue::IsA(key,actor_class))&&!(component_class&&ue::IsA(key,component_class));};  // a subsystem
    // Nothing to do unless a list on the hero or on a world subsystem (no actor) holds handles; its newest one decides once.
    int32_t newest=0;bool mine=false,hero_held=false;
    for(const SlomoEntry& e:entries){if(!e.list->num||!ue::Alive(e.key)||(e.key!=hero&&!world_key(e.key)))continue;
        mine=true;hero_held|=e.key==hero;newest=static_cast<int32_t*>(e.list->data)[e.list->num-1];}
    if(!mine||SlomoSeenBefore(action,newest))return;
    auto moved=std::find_if(slomo_moved_.begin(),slomo_moved_.end(),[&](const SlomoMoved& m){return m.action.object==action&&Same(m.action);});
    const bool npc_hero=RunsMove(hero,[](Object* a){return NpcMoveClass(a);});
    const auto pair=[&](Object* pawn){return pawn!=hero&&npc_hero&&RunsMove(pawn,[](Object* a){return ReceivingMove(a);});};
    // The other keys holding handles: the hero's grab pair is ours, any other character or actor is not (and not ones
    // DriveSlomo gave this action's slow before).
    std::string others;
    for(const SlomoEntry& e:entries){if(!e.list->num||!ue::Alive(e.key)||e.key==hero||world_key(e.key))continue;
        const bool given=moved!=slomo_moved_.end()&&std::find(moved->ours.begin(),moved->ours.end(),e.key)!=moved->ours.end();
        if(!given&&!(pawn_class&&ue::IsA(e.key,pawn_class)&&pair(e.key)))others+=(others.empty()?"":",")+Text(e.key);}
    // Who cast it.
    Object* ability=nullptr,*caster=nullptr,*ga=Class("/Script/GameplayAbilities.GameplayAbility");
    if(ue::FindFunction(ue::ClassOf(action),"GetAbility")){Call get(action,"GetAbility");if(get.Invoke())ability=get.Get<Object*>("ReturnValue");}
    if(!ue::Alive(ability)||!ga||!ue::IsA(ability,ga)){ability=nullptr;Object* o=ue::OuterOf(action);
        for(size_t guard=0;o&&guard<8&&!ability;++guard,o=ue::OuterOf(o))if(ga&&ue::IsA(o,ga))ability=o;}
    if(ue::FindFunction(ue::ClassOf(action),"GetAvatarActor")){Call get(action,"GetAvatarActor");if(get.Invoke())caster=get.Get<Object*>("ReturnValue");}
    if(!ue::Alive(caster)&&ability){Call get(ability,"GetAvatarActorFromActorInfo");if(get.Invoke())caster=get.Get<Object*>("ReturnValue");}
    if(!ue::Alive(caster))caster=nullptr;
    const std::string move=ability?Text(ue::ClassOf(ability)):"?";
    const Vector hero_at=SlomoActorAt(hero),caster_at=caster?SlomoActorAt(caster):hero_at;
    const char* side=nullptr;
    if(!ability||!NpcMoveClass(ability))side=nullptr;
    else if(caster==hero)side="hero";
    else if(caster&&SlomoDistance(caster_at,hero_at)>kSlomoReachCm)side="remote";
    else if(caster&&TeamAttitude(caster,hero)==0)side="ally";
    else if(caster&&ReceivingMove(ability)&&npc_hero)side="grab-victim";
    if(!side||!others.empty()){
        Logf("SLOMO_SIDE left action=%s ability=%s caster=%s heroHeld=%d: %s",Text(ue::ClassOf(action)).c_str(),move.c_str(),Text(caster).c_str(),hero_held?1:0,
             !others.empty()?("it also holds "+others).c_str():!ability?"no ability":!NpcMoveClass(ability)?"not an NPC move":"a foe's slow on the hero");
        return;
    }
    // Everyone else around the caster takes the action's own slow.
    Object* function=ue::FindFunction(ue::ClassOf(action),"Slomo");const double total=SlomoActionLength(action);
    std::vector<Object*> targets;
    {Call actors(ue::Default("/Script/Engine.Default__GameplayStatics"),"GetAllActorsOfClass");
     if(pawn_class&&actors.Set("WorldContextObject",hero).Set("ActorClass",pawn_class).Invoke()){
        const Array list=actors.Get<Array>("OutActors");
        if(list.data&&list.num>0&&list.num<=2048&&FaceReadable(list.data,size_t(list.num)*sizeof(Object*)))
            for(int32_t i=0;i<list.num;++i){Object* pawn=static_cast<Object**>(list.data)[i];
                if(!ue::Alive(pawn)||pawn==hero||pawn==caster||ue::IsTemplate(pawn))continue;
                const int32_t dilation=Offset(pawn,"CustomTimeDilation");if(dilation<0||ue::At<float>(pawn,dilation)<0.01f)continue;  // frozen (the audit's dummy)
                if(SlomoDistance(SlomoActorAt(pawn),caster_at)>kSlomoReachCm||pair(pawn)||!ImplementsSlomoTarget(pawn))continue;
                targets.push_back(pawn);}
        ue::Free(list.data);}}
    std::string shape;size_t given=0;
    if(function)for(Object* pawn:targets)if(ActionSlomo(action,function,pawn,total,shape))++given;
    if(function&&std::find(slomo_logged_functions_.begin(),slomo_logged_functions_.end(),function)==slomo_logged_functions_.end()){
        slomo_logged_functions_.push_back(function);Logf("SLOMO_SIDE %s.Slomo parameters: %s",Text(ue::ClassOf(action)).c_str(),shape.c_str());}
    // Then the handles on the hero, the pair and a subsystem stop and leave the action's lists (its end skips them).
    const Name id=Property<Name>(action,"Slomo ID");size_t stopped=0,failed=0,held=0;std::string from;
    if(!SlomoEntries(action,entries,&why)){Logf("SLOMO_SIDE %s: Handles unread after the slow went out (%s); the hero keeps it",move.c_str(),why);return;}
    for(const SlomoEntry& e:entries){if(!e.list->num||!ue::Alive(e.key))continue;
        if(std::find(targets.begin(),targets.end(),e.key)!=targets.end()){++held;continue;}
        if(!(e.key==hero||world_key(e.key)||(pawn_class&&ue::IsA(e.key,pawn_class)&&pair(e.key))))continue;
        Object* stop=SlomoTargetFunction(e.key,"StopSlomo");bool all=stop!=nullptr;
        for(int32_t i=0;stop&&i<e.list->num;++i){Call call(e.key,stop);
            if(call.Set("SlomoID",id).Set("Handle",static_cast<int32_t*>(e.list->data)[i]).Invoke())++stopped;else{++failed;all=false;}}
        if(all)e.list->num=0;
        from+=(from.empty()?"":",")+Text(e.key);}
    if(moved==slomo_moved_.end()){slomo_moved_.push_back({Id(action),{}});moved=slomo_moved_.end()-1;}
    for(Object* pawn:targets)if(std::find(moved->ours.begin(),moved->ours.end(),pawn)==moved->ours.end())moved->ours.push_back(pawn);
    slomo_check_={GetTickCount64()+300,GetTickCount64()+1000,{},move};for(Object* pawn:targets)slomo_check_.targets.push_back(Id(pawn));
    Logf("SLOMO_SIDE moved action=%s ability=%s caster=%s side=%s id=%s length=%.2fs: slowed %zu of %zu characters (%zu hold handles), stopped %zu handles (%zu failed) on %s; hero dilation %.2f",
         Text(ue::ClassOf(action)).c_str(),move.c_str(),Text(caster).c_str(),side,Text(id).c_str(),total,given,targets.size(),held,stopped,failed,from.empty()?"-":from.c_str(),HeroDilation(hero));
}
// Every frame: nothing until the hero (or the world) runs slow; then the slomo actions are looked at every kSlomoScanMs.
void DriveSlomo(){
    if(MovePreview::HoldsGameplay())return;  // the preview holds the hero at 0: no slow motion to move
    Context context;if(!ContextNow(context))return;Object* hero=context.pawn;
    const ULONGLONG now=GetTickCount64();
    for(ULONGLONG* when:{&slomo_check_.at,&slomo_check_.last})if(*when&&now>=*when){
        std::string list;for(const Identity& t:slomo_check_.targets){const int32_t at=Same(t)?Offset(t.object,"CustomTimeDilation"):-1;
            char row[96];std::snprintf(row,sizeof(row),"%s%s=%.2f",list.empty()?"":", ",Text(t.object).c_str(),at>=0?ue::At<float>(t.object,at):-1.f);list+=row;}
        Logf("SLOMO_SIDE check %s +%s: hero dilation %.2f; %s",slomo_check_.move.c_str(),when==&slomo_check_.at?"300ms":"1s",HeroDilation(hero),list.empty()?"no characters":list.c_str());*when=0;}
    // A drop in the hero's dilation (a new slow) looks at once; otherwise every kSlomoScanMs while it or the world is slow.
    const float dilation=HeroDilation(hero);
    const bool drop=dilation>=0&&dilation<0.98f&&(slomo_last_dilation_>=0.98f||dilation<slomo_last_dilation_*0.7f);
    if(dilation>=0)slomo_last_dilation_=dilation;
    if(!drop&&now<slomo_scan_at_)return;
    slomo_scan_at_=now+kSlomoScanMs;
    bool slowed=dilation>=0&&dilation<0.98f;float world=1;
    if(!slowed){Call get(ue::Default("/Script/Engine.Default__GameplayStatics"),"GetGlobalTimeDilation");
        if(get.Set("WorldContextObject",hero).Invoke())world=get.Get<float>("ReturnValue");
        slowed=world<(audit_speed_world_?audit_speed_:1.0f)*0.98f;}
    if(!slowed)return;
    if(now>=slomo_class_at_){slomo_class_at_=now+kSlomoClassMs;for(size_t i=0;i<std::size(kSlomoClasses);++i)if(!ue::Alive(slomo_classes_[i]))slomo_classes_[i]=Class(kSlomoClasses[i]);}
    if(std::none_of(std::begin(slomo_classes_),std::end(slomo_classes_),[](Object* c){return c!=nullptr;}))return;
    // The whole object array (~7 ms) only on a drop (at most every 500 ms) or every 2 s while slow; the actions found
    // there (an ability instance's timeline keeps its own, made when the ability is) are looked at in between.
    if((drop&&now>=slomo_full_at_+500)||now>=slomo_full_at_+2000||slomo_cache_.empty()){
        const LONGLONG t0=QpcNow();slomo_cache_.clear();slomo_full_at_=now;
        ue::ForEachObject([&](Object* o){Object* c=ue::ClassOf(o);for(Object* k:slomo_classes_)if(k&&c==k){if(!ue::IsDefault(o))slomo_cache_.push_back(Id(o));break;}return true;});
        if(slomo_scans_logged_<3){++slomo_scans_logged_;Logf("SLOMO_SIDE scan: %zu slomo actions in %.2f ms (hero dilation %.2f, world %.2f)",slomo_cache_.size(),QpcMs(t0),dilation,world);}
    }
    std::vector<Object*> found;found.reserve(slomo_cache_.size());for(const Identity& a:slomo_cache_)if(Same(a))found.push_back(a.object);
    slomo_moved_.erase(std::remove_if(slomo_moved_.begin(),slomo_moved_.end(),[](const SlomoMoved& m){return !Same(m.action);}),slomo_moved_.end());
    for(Object* a:found)ConsiderSlomo(a,hero);
}
// What each frame's drivers cost on the game thread while a chain runs (NPC_PERF chain, one line per 10 s with
// chain frames): average and largest ms per frame of each group, the whole frame's share, and FindObject's work.
enum ChainPart : size_t { kPartPool, kPartDisplay, kPartSkill, kPartLooks, kPartCombo, kPartKeys, kPartInput, kPartTriggers, kPartComboKeys, kPartDamage, kPartPreload, kPartUpkeep, kPartCount };
struct ChainCost { double sum[kPartCount]{},max[kPartCount]{},frame_sum=0,frame_max=0;uint32_t frames=0;ULONGLONG since=0;ue::LookupStats lookups;size_t part_scans=0; };
ChainCost chain_cost_;double chain_frame_[kPartCount]{};bool chain_frame_on_=false;
template<class F> void Timed(ChainPart part,F&& work){const LONGLONG t0=QpcNow();work();chain_frame_[part]+=QpcMs(t0);}
void CommitChainFrame(){
    const bool chain=chain_frame_on_||combo_.trigger>=0;
    if(chain){
        ChainCost& c=chain_cost_;
        if(!c.frames){c.since=GetTickCount64();c.lookups=ue::LookupTotals();c.part_scans=part_scans_;}
        double frame=0;for(size_t i=0;i<kPartCount;++i){c.sum[i]+=chain_frame_[i];c.max[i]=(std::max)(c.max[i],chain_frame_[i]);frame+=chain_frame_[i];}
        c.frame_sum+=frame;c.frame_max=(std::max)(c.frame_max,frame);++c.frames;}
    std::fill(std::begin(chain_frame_),std::end(chain_frame_),0.0);
    ChainCost& c=chain_cost_;
    if(c.frames&&GetTickCount64()>=c.since+10000){
        static const char* const kNames[kPartCount]={"pool","display","skill","looks","combo","rawKeys","heroInput","heroTriggers","comboKeys","damage","preload","upkeep"};
        std::string parts;char row[64];
        for(size_t i=0;i<kPartCount;++i){std::snprintf(row,sizeof(row)," %s=%.3f/%.2f",kNames[i],c.sum[i]/c.frames,c.max[i]);parts+=row;}
        const ue::LookupStats now=ue::LookupTotals();
        Logf("NPC_PERF chain %llus frames=%u frameMs avg=%.3f max=%.2f (ms avg/max per frame:%s) lookups cached=%llu engine=%llu/%llu scans=%llu scanMs=%.1f partScans=%zu",
             (GetTickCount64()-c.since)/1000,c.frames,c.frame_sum/c.frames,c.frame_max,parts.c_str(),now.hits-c.lookups.hits,now.engine_found-c.lookups.engine_found,
             now.engine_missed-c.lookups.engine_missed,now.scans-c.lookups.scans,now.scan_ms-c.lookups.scan_ms,part_scans_-c.part_scans);
        chain_cost_={};}
}
struct ChainFrame { ChainFrame(){chain_frame_on_=combo_.trigger>=0;} ~ChainFrame(){CommitChainFrame();} };
struct UpkeepTime { LONGLONG t0=QpcNow(); ~UpkeepTime(){chain_frame_[kPartUpkeep]+=QpcMs(t0);} };
// 过场动画: a cutscene shows the game's own copy of the hero (CutsceneHero) with the player pawn hidden, so the look the
// pawn wears (or the one waiting to come back after a scene change) goes onto that copy for the cutscene: its own parts
// hidden and the NPC display copying its pose, as on the pawn, without the gameplay side (body collision, hurt voice,
// camera height, skills, chains). When the cutscene ends or the copy goes, the look comes back onto the pawn as after a
// scene change (KeepForNextScene, ResumeAfterScene).
void CutsceneRetryFor(Object* actor,Object* mesh) {
    Object* anim=ue::Alive(mesh)?AnimInstance(mesh):nullptr;
    if(!Same(cutscene_retry_actor_)||cutscene_retry_actor_.object!=actor||!Same(cutscene_retry_mesh_)||cutscene_retry_mesh_.object!=mesh||
       cutscene_retry_anim_.object!=anim||(anim&&!Same(cutscene_retry_anim_))){
        cutscene_retry_actor_=Id(actor);cutscene_retry_mesh_=Id(mesh);cutscene_retry_anim_=Id(anim);cutscene_retry_.Reset();}
}
void CutsceneRetryFailed() {
    cutscene_retry_.Failed(GetTickCount64());
    Logf("NPC_RETARGET cutscene retry failures=%u next=%llu",cutscene_retry_.failures,cutscene_retry_.next);
}
void DriveCutsceneLook(const Context& context) {
    const int32_t wanted=active_&&!cleanup_pending_?selected_:!active_&&!HasResources()?resume_entry_:-1;
    if(wanted<0||static_cast<size_t>(wanted)>=std::size(kProfiles)||Auditing()||borrow_.stage!=Borrow::Stage::Off||weapon_.stage!=WeaponHold::Stage::Off)return;
    if(!SequencePlaying(context)){if(!cutscene_host_){cutscene_retry_actor_={};cutscene_retry_.Reset();}return;}
    Object* copy=CutsceneHero(context);if(!copy)return;
    Object* mesh=CutsceneMesh(copy);
    if(cutscene_host_&&Same(pawn_)&&pawn_.object==copy&&Same(primary_)&&primary_.object==mesh)return;
    CutsceneRetryFor(copy,mesh);if(!cutscene_retry_.Ready(GetTickCount64()))return;
    Logf("NPC_RETARGET cutscene: bound hero %s (%s); id=%s attempt=%u",Text(copy).c_str(),Text(ue::ClassOf(copy)).c_str(),kProfiles[wanted].id,cutscene_retry_.failures+1);
    if(active_){KeepForNextScene("cutscene-host-switch");if(!Restore()){Status(L"过场角色切换：正在清理上一宿主的临时外观");return;}}
    Context host{context.world,copy,mesh};
    cutscene_host_=true;failure_text_.clear();
    if(Apply(host,wanted)){
        cutscene_playback_.Hold(true,true,GetTickCount64());
        Status(L"过场动画：NPC %s 外观已套在过场角色上；过场结束后回到主角",kProfiles[wanted].name);
        Logf("NPC_RETARGET cutscene: id=%s on %s",kProfiles[wanted].id,Text(copy).c_str());return;}
    cutscene_host_=false;CutsceneRetryFailed();
    if(resume_entry_!=wanted)resume_tries_=0;
    resume_entry_=wanted;resume_at_=0;
    Logf("NPC_RETARGET cutscene: id=%s refused on %s (%s); bounded retry pending",kProfiles[wanted].id,Text(copy).c_str(),
         failure_text_.empty()?"-":Utf8(failure_text_.c_str()).c_str());
}

// ---- 派生插件 (ComboPlugins) ----------------------------------------------------------------------------------------
namespace {
// The hero's (or an enemy's) health and its maximum (AttributeSetBase Health / MaxHealth; false: unreadable).
bool PluginHealth(Object* pawn,float& value,float& most){
    value=0;most=0;
    Object* set=ue::Alive(pawn)?Property<Object*>(pawn,"AttributeSetBase"):nullptr,*data=Class("/Script/GameplayAbilities.GameplayAttributeData");
    if(!ue::Alive(set)||!data||ue::StructSize(data)!=16||ue::Offset(data,"CurrentValue")!=12)return false;
    const int32_t health=Offset(set,"Health"),max_at=Offset(set,"MaxHealth");
    if(health<0||!FaceReadable(set+health+12,sizeof(float)))return false;
    value=ue::At<float>(set,health+12);
    if(max_at>=0&&FaceReadable(set+max_at+12,sizeof(float)))most=ue::At<float>(set,max_at+12);
    if(!std::isfinite(value)||value<0||value>1e9f){value=0;return false;}
    if(!std::isfinite(most)||most<0||most>1e9f)most=0;
    return true;
}
std::string PluginLook(){return active_?LookName(selected_):"hero";}
const NpcCombo::Config* PluginConfig(){return combo_.owner!=-2?&combo_.config:nullptr;}
int PluginTrigger(){return combo_.trigger;}
bool PluginState(PbzChainState& s){
    s.ready=combo_.owner!=-2&&combo_.config.enabled?1:0;
    s.trigger=combo_.trigger;s.cursor=combo_.cursor;s.playing=combo_.playing;s.queued=combo_.queued.move;s.button=combo_.button;
    s.phase=combo_.phase==ComboRun::Phase::Running?PBZ_PHASE_RUNNING:combo_.phase==ComboRun::Phase::Starting?PBZ_PHASE_STARTING:PBZ_PHASE_IDLE;
    s.look_changing=borrow_.stage==Borrow::Stage::Going||borrow_.stage==Borrow::Stage::Returning?1:0;
    s.visit=combo_.visit;s.generation=combo_.generation;
    s.move_started_ms=combo_.playing>=0?combo_.started:0;s.reset_at_ms=combo_.phase==ComboRun::Phase::Idle?combo_.reset_at:0;
    strncpy_s(s.table,ComboProfile(),_TRUNCATE);strncpy_s(s.look,PluginLook().c_str(),_TRUNCATE);
    return true;
}
// get_hero: read at most once a frame (the nearest enemy is a search).
uint64_t plugin_hero_frame_=0,plugin_frame_=0;PbzHeroFacts plugin_hero_{};
bool PluginHero(PbzHeroFacts& f){
    if(plugin_hero_frame_==plugin_frame_&&plugin_hero_.size){const uint32_t size=f.size;f=plugin_hero_;f.size=size;return f.valid!=0;}
    PbzHeroFacts out{};out.size=sizeof(out);
    Context context;
    if(ContextNow(context)&&ue::Alive(context.pawn)){
        out.valid=1;PluginHealth(context.pawn,out.health,out.health_max);out.airborne=Airborne(context.pawn)?1:0;
        Vector self{};{Call where(context.pawn,"K2_GetActorLocation");if(where.Invoke()){self=where.Get<Vector>("ReturnValue");out.x=self.x;out.y=self.y;out.z=self.z;}}
        unsigned char lock[16]{};Object* enemy=nullptr;const bool locked=HeroLockTarget(lock,enemy);
        if(ue::Alive(enemy)){
            Call where(enemy,"K2_GetActorLocation");
            if(where.Invoke()){const Vector p=where.Get<Vector>("ReturnValue");out.has_target=1;out.target_locked=locked?1:0;
                out.target_distance=static_cast<float>(std::sqrt((p.x-self.x)*(p.x-self.x)+(p.y-self.y)*(p.y-self.y)+(p.z-self.z)*(p.z-self.z)));
                PluginHealth(enemy,out.target_health,out.target_health_max);}
        }
        std::string weapon;if(WeaponPresets::CurrentMainWeaponId(weapon))strncpy_s(out.weapon,weapon.c_str(),_TRUNCATE);
        strncpy_s(out.look,PluginLook().c_str(),_TRUNCATE);
    }
    plugin_hero_=out;plugin_hero_frame_=plugin_frame_;
    const uint32_t size=f.size;f=out;f.size=size;return out.valid!=0;
}
bool PluginPress(int button,const char* source){
    if(!ComboReady()||!Same(ChainPawn())){Logf("NPC_COMBO plugin press %s button=%d: no chain now",source,button);return false;}
    NpcCombo::Press press;
    if(button==static_cast<int>(NpcCombo::Trigger::NpcKey))press.key=true;else press.bits=1u<<button;
    if(button==static_cast<int>(NpcCombo::Trigger::Execution)){std::string id;if(WeaponPresets::CurrentMainWeaponId(id))press.weapon=id;}
    const bool taken=PressCombo(press,source);
    Logf("NPC_COMBO plugin press %s button=%d -> %s",source,button,taken?"taken":"not taken");
    return taken;
}
// play_node: move node `node` now (the move playing is cut, no 派生点 waited for); with no chain going on, one starts
// at the start whose wires reach it.
bool PluginPlay(int node,int button){
    if(!ComboReady()||!Same(ChainPawn()))return false;
    const NpcCombo::Node* n=ComboNode(node);if(!n||!NpcCombo::IsMove(*n))return false;
    const ULONGLONG now=GetTickCount64();
    if(combo_.trigger<0){
        const int start=NpcCombo::TriggerOf(combo_.config,static_cast<size_t>(node));const NpcCombo::Node* t=ComboNode(start);if(!t)return false;
        ResetCombo("plugin-play",L"");
        combo_.trigger=start;combo_.cursor=start;combo_.button=button>=0?button:t->trigger;combo_.reset_ms=NpcCombo::ClampReset(t->ms);
        ++combo_.visit;combo_.path_clock.Reset();combo_.ended_at=0;combo_.row_branch=-1;combo_.row_row=-1;
        PluginEvent(PBZ_EVENT_CHAIN_START,start,"plugin-play");
    }
    NpcCombo::Hop hop;hop.move=node;hop.button=button>=0?button:combo_.button;
    Logf("NPC_COMBO plugin play node=%d button=%d trigger=%d",node,hop.button,combo_.trigger);
    PlayHop(hop,now);return true;
}
void PluginReset(const char* reason){
    if(combo_.trigger<0)return;
    Logf("NPC_COMBO plugin reset %s",reason);
    ResetCombo(reason,L"派生链停止：插件结束了派生链");
}
bool PluginCancel(int32_t handle){
    Context context;if(!ContextNow(context)||!ue::Alive(context.pawn))return false;
    const bool cancelled=CancelSpec(PawnAsc(context.pawn),handle);
    Logf("NPC_COMBO plugin cancel ability handle=%d -> %d",handle,cancelled?1:0);
    return cancelled;
}
bool PluginWatched(int code){return std::find(raw_.watched.begin(),raw_.watched.end(),code)!=raw_.watched.end();}
void PluginLog(const char* text){Logf("%s",text);}
void PluginNotice(const wchar_t* title,const wchar_t* detail){SkillToast::Show(title?title:L"",detail?detail:L"");}
// "Default__GA_Hun_Young_Air_A_C" / "GA_Hun_Young_Air_A_C_3" -> "GA_Hun_Young_Air_A".
std::string PluginAbilityName(Object* ability){
    std::string name=Text(ability);const size_t dot=name.find_last_of("./");if(dot!=std::string::npos)name=name.substr(dot+1);
    if(name.rfind("Default__",0)==0)name=name.substr(9);
    const size_t c=name.rfind("_C");
    if(c!=std::string::npos&&c+2<=name.size()&&(c+2==name.size()||(name[c+2]=='_'&&name.find_first_not_of("0123456789",c+3)==std::string::npos)))name.resize(c);
    return name;
}
// on_hero_ability: each hero ability as it starts (one not active the frame before); a plugin's PBZ_CONSUME cancels it.
std::vector<int32_t> plugin_seen_;bool plugin_seen_set_=false;
void DrivePluginAbilities(){
    if(!ComboPlugins::WantsAbilities()){plugin_seen_.clear();plugin_seen_set_=false;return;}
    Context context;if(!ContextNow(context)||!ue::Alive(context.pawn)){plugin_seen_.clear();plugin_seen_set_=false;return;}
    Object* asc=PawnAsc(context.pawn);if(!ue::Alive(asc))return;
    Object* type=Class("/Script/GameplayAbilities.GameplayAbilitySpec"),*container=Class("/Script/GameplayAbilities.GameplayAbilitySpecContainer");
    const int32_t specs=Offset(asc,"ActivatableAbilities"),items=container?ue::Offset(container,"Items"):-1,size=type?ue::StructSize(type):0,
        handle_at=type?ue::Offset(type,"Handle"):-1,ability_at=type?ue::Offset(type,"Ability"):-1,count_at=type?ue::Offset(type,"ActiveCount"):-1;
    if(specs<0||items<0||size<=0||handle_at<0||ability_at<0||count_at<0)return;
    const Array array=ue::At<Array>(asc,specs+items);
    if(array.num<0||array.num>4096||array.max<array.num||(array.num&&!FaceReadable(array.data,static_cast<size_t>(array.num)*static_cast<size_t>(size))))return;
    std::vector<int32_t> running;std::vector<std::pair<int32_t,Object*>> started;
    for(int32_t i=0;i<array.num;++i){Object* row=static_cast<Object*>(array.data)+static_cast<size_t>(i)*static_cast<size_t>(size);
        if(!ue::At<uint8_t>(row,count_at))continue;
        const int32_t handle=ue::At<int32_t>(row,handle_at);running.push_back(handle);
        if(plugin_seen_set_&&std::find(plugin_seen_.begin(),plugin_seen_.end(),handle)==plugin_seen_.end())started.push_back({handle,ue::At<Object*>(row,ability_at)});}
    const bool first=!plugin_seen_set_;plugin_seen_=std::move(running);plugin_seen_set_=true;
    if(first)return;  // what was active before the plugins listened is no start
    for(const auto& [handle,ability]:started){  // read first: a cancel may change the spec array
        const std::string name=PluginAbilityName(ability);
        PbzAbilityEvent e{};e.size=sizeof(e);e.name=name.c_str();e.bits=AbilityActionBits(ability);e.handle=handle;
        e.chain_own=(skill_.handle>0&&handle==skill_.handle)||(pending_.handle>0&&handle==pending_.handle)?1:0;e.now_ms=GetTickCount64();
        if(ComboPlugins::HeroAbility(e)!=PBZ_CONSUME)continue;
        const bool cancelled=CancelSpec(asc,handle);
        Logf("NPC_COMBO plugin consumed hero %s bits=0x%x handle=%d cancelled=%d",name.c_str(),e.bits,handle,cancelled?1:0);
    }
}
// Once a frame before the chain: the look and the chain's tables as events, then the plugins' frame and their hero
// ability hook.
std::string plugin_look_;uint64_t plugin_generation_=0;int32_t plugin_owner_=-3;
void DrivePlugins(){
    ++plugin_frame_;
    const ULONGLONG now=GetTickCount64();
    if(ComboPlugins::Listening()){
        const std::string look=PluginLook();
        if(look!=plugin_look_){if(!plugin_look_.empty())PluginEvent(PBZ_EVENT_LOOK,-1,look.c_str());plugin_look_=look;}
        if(combo_.generation!=plugin_generation_||combo_.owner!=plugin_owner_){
            if(plugin_owner_!=-3)PluginEvent(PBZ_EVENT_TABLES,-1,ComboProfile());
            plugin_generation_=combo_.generation;plugin_owner_=combo_.owner;}
    }
    ComboPlugins::Frame(now);
    DrivePluginAbilities();
}
}  // namespace

void Tick() {
    if (!initialized_) return;
    ChainFrame chain_frame;
    Timed(kPartPool,[]{HoldPoolDormancy();});
    const LONGLONG display_t0 = QpcNow();
    if (active_ && !cleanup_pending_ && !RefreshFaceVisibility()) {
        cleanup_pending_=true;
        Status(L"NPC 独立面部节点 / 原生评估状态变化，保留清理记录并于空闲时恢复原模型");
        Logf("NPC_FACE_NEUTRAL node identity/evaluation failed; restore requested, shared CDO untouched");
    }
    if (active_ && !cleanup_pending_ && source_driver_mode_) EnforceHiddenFlags();
    if (active_) ProbeCameraLift();
    chain_frame_[kPartDisplay] += QpcMs(display_t0);
    // The NPC skill key: the next move of the chain wired to its node on the 招式派生 page, else the next skill of
    // the random round. Without a look: the hero's own chain (owner -1: its BOSS 技能 / 援护 / hero moves); nothing
    // else happens when that chain does not take the press.
    if (InterlockedExchange(&skill_request_, 0) && !cutscene_host_) {
        std::string keys;
        if (NpcKeyInRawKeys(keys) && !NpcKeyLeads())
            Logf("NPC_COMBO npc key: its chord is part of 自定义按键 [%s] and the chain takes no 按键 press here: left to those keys (no cast, the chain stays at node=%d)",
                 keys.c_str(), combo_.cursor);
        else if (active_) { if (!PressNpcKey()) CastSkill(); }
        else if (!PressNpcKey()) Logf("NPC_COMBO npc key without a look: the hero's chain takes no 按键 press now (ready=%d)", combo_.owner == -1 ? 1 : 0);
    }
    if (const LONG pick = InterlockedExchange(&cast_request_, 0); pick > 0 && active_ && !cutscene_host_ && !CastExact(static_cast<size_t>(pick - 1))) Logf("NPC_SKILL cast %ld refused: no skin / busy / no such skill", pick);
    if (cutscene_host_) DriveBlink();  // the look on a cutscene copy: no skills, chains, keys or damage meanwhile
    else {
    if (const LONG test = InterlockedExchange(&bare_test_, 0)) {
        const size_t skin = static_cast<size_t>((test >> 16) - 1), skill = static_cast<size_t>(test & 0xffff); Context c;
        if (!active_ && ContextNow(c) && skin < std::size(kNpcSkillSets) && skill >= 1 && skill <= kNpcSkillSets[skin].skill_count) {
            const bool ok = CastMove(c, kNpcSkillSets[skin].skills[skill - 1], L"BOSS 技能（测试）", kChainWaitMs);
            Logf("NPC_TEST bare cast %s ok=%d skillBare=%d pendingBare=%d hasResources=%d", NpcCombo::ShortName(kNpcSkillSets[skin].skills[skill - 1]).c_str(), ok ? 1 : 0,
                 skill_.bare ? 1 : 0, pending_.bare ? 1 : 0, HasResources() ? 1 : 0);
        } else Logf("NPC_TEST bare refused: active=%d", active_ ? 1 : 0);
    }
    if (const LONG bits = InterlockedExchange(&press_test_, 0)) {
        NpcCombo::Press press; press.bits = static_cast<unsigned>(bits);
        const LONG hold = InterlockedExchange(&press_test_hold_, 0);
        test_hold_until_ = GetTickCount64() + static_cast<ULONGLONG>((std::max)(hold, 0L));
        const bool ready = ComboReady(); HoldStart held{};
        if (ready) held = PressOrHold(press, "test", HoldKind::Hero, press.bits);
        Logf("NPC_TEST press bits=0x%lx hold=%ldms ready=%d waiting=%d taken=%d owner=%d", bits, hold, ready ? 1 : 0, held.waiting ? 1 : 0, held.taken ? 1 : 0, combo_.owner);
    }
    if (InterlockedExchange(&loco_probe_, 0)) LogLocoStatus();
    {bool probe=false;AcquireSRWLockShared(&bone_probe_lock_);probe=!bone_probe_.empty();ReleaseSRWLockShared(&bone_probe_lock_);if(probe)LogBoneProbe();}
    if (InterlockedExchange(&resource_probe_, 0))
        Logf("NPC_TEST resources has=%d active=%d skill=%d(bare %d owned %d) pending=%d(bare %d) skillPins=%zu barePins=%zu weapons=%zu hidden=%zu bareWeapons=%d colliders=%zu standIns=%zu voice=%zu comboOwner=%d",
             HasResources() ? 1 : 0, active_ ? 1 : 0, skill_.handle, skill_.bare ? 1 : 0, skill_.owned ? 1 : 0, pending_.handle, pending_.bare ? 1 : 0, skill_pins_.size(), bare_pins_.size(),
             skill_weapons_.size(), hidden_weapons_.size(), bare_weapons_ ? 1 : 0, skill_colliders_.size(), stand_ins_.size(), voice_slots_.size(), combo_.owner);
    Timed(kPartSkill, [&] {
        if (active_) { RetryPendingSkill(); HoldSkillLock(); HoldForeignMontage(); DrivePuppet(); } // every frame
        else if (pending_.owned || pending_.bare) RetryPendingSkill();  // the hero's own chain move, or a bare boss move (不变身)
        if (!active_ || own_retarget_.skeleton.object) HoldOwnRetarget();  // 不变身: another skeleton's montage on the hero's own body
        if (!active_ && (skill_.owned || skill_.bare)) WatchSkill();  // and its end, also after the chain stopped (no look watches it then)
        if (!active_ && bare_weapons_) { if (skill_.handle > 0 || pending_.handle > 0) HoldBareWeapons(ChainPawn().object); else RemoveSkillWeapons("bare-move-gone"); }
    });
    Timed(kPartLooks, [&] { DriveBody(); DriveBlink(); DriveGrabBodies(); DriveBorrow(); DriveWeapon(); DriveTeleport(); DriveHookProbe(); DriveHookClose(); DriveHookAnchor(); DriveAir(); DriveViewFollow(); DriveViewProbe(); DriveTagDump(); DriveAbilityProbe(); DriveTimelineWatch(); PublishChain(); });
    if (const LONG asked = InterlockedExchange(&cam_offset_test_, 0)) {  // tests: npcretarget camoffset x y z | off
        Context c; Object* camera = ContextNow(c) ? HeroCamera(c.pawn) : nullptr;
        const Vector offset = asked == 2 ? Vector{} : cam_offset_value_;
        Logf("NPC_TEST camoffset (%.0f,%.0f,%.0f) space=%s camera=%s ok=%d", offset.x, offset.y, offset.z, view_space_ ? "local" : "world", Text(camera).c_str(),
             camera && SendViewOffset(camera, c.pawn, offset, 0.2f) ? 1 : 0);
    }
    if (InterlockedExchange(&lock_struct_probe_, 0)) { Context c; if (ContextNow(c)) ProbeLockStruct(c.pawn); }
    // every frame; all stop once the skin is off
    DrivePlugins();  // 派生插件: before the chain, so their presses and signals are read this frame
    Timed(kPartCombo, [&] { DriveCombo(); DriveHeroSize(); }); Timed(kPartKeys, [&] { DriveRawKeys(); DriveHold(); }); Timed(kPartInput, [&] { DriveHeroInput(); });
    Timed(kPartTriggers, [&] { DriveHeroTriggers(); DriveOwnAir(); }); Timed(kPartComboKeys, [&] { DriveComboKeys(); PublishChainPosition(); });
    Timed(kPartDamage, [&] { ScaleSkillDamage(); DriveAssistEffects(); DriveBossCalls(); HoldNoExecution(); DriveSlomo(); DriveChainFx(); }); // every frame; all close what they hold once the skin is off
    }
    Timed(kPartPreload, [&] { PreloadStep(false); });  // BOSS 技能 preloads in flight (async)
    AuditTick();
    int32_t action = -1, entry = -1;
    AcquireSRWLockExclusive(&request_lock); action = pending_action_; entry = pending_entry_; pending_action_ = -1; ReleaseSRWLockExclusive(&request_lock);
    if (InterlockedExchange(&forget_resume_, 0)) ForgetResume(action == static_cast<int32_t>(Action::Restore) ? "restore requested" : "another request");
    if (action >= 0) { waiting_action_ = action; waiting_entry_ = entry; wait_deadline_ = GetTickCount64() + 60000; blink_wanted_ = action != static_cast<int32_t>(Action::Inspect); look_cancel_ = false; }
    if (action < 0 && GetTickCount64() < next_tick_) return;
    next_tick_ = GetTickCount64() + 250;
    PerfReport();PerfScope timing(PerfKind::Maintenance);UpkeepTime upkeep_time;  // a look change in here shows as upkeep max (its own line: apply timing)
    const bool disabled = InterlockedCompareExchange(&disabled_, 0, 0) != 0;
    if (disabled && waiting_action_ != static_cast<int32_t>(Action::Restore)) {
        PoolClear();AcquireSRWLockExclusive(&snapshot_lock); snapshot_.ready = false; ReleaseSRWLockExclusive(&snapshot_lock);
        Publish(); return;
    }
    Context context; const bool context_ok = ContextNow(context);
    AcquireSRWLockExclusive(&snapshot_lock); snapshot_.ready = context_ok && !disabled; ReleaseSRWLockExclusive(&snapshot_lock);
    DrivePreload(context_ok ? &context : nullptr);
    // On a cutscene copy (cutscene_host_) the look stays while the copy lives and a cutscene plays; then it goes back
    // onto the pawn (soon: the cutscene's end is no scene load).
    if (active_ && (!Same(primary_) || !Same(pawn_) || (!source_driver_mode_&&!Same(backup_)) || (source_driver_mode_&&!Same(display_)) || !context_ok ||
                    (cutscene_host_ ? !cutscene_playback_.Hold(SequencePlaying(context),true,GetTickCount64()) : context.pawn != pawn_.object))) {
        const bool host = cutscene_host_;
        KeepForNextScene(host ? "cutscene-ended" : "player-or-map-changed");
        const bool restored = Restore();
        if (host && restored && resume_entry_ >= 0) resume_at_ = GetTickCount64() + 300;
        Status(restored ? (host ? L"过场动画结束，NPC 外观回到主角身上" : L"玩家或地图已变化，NPC 模型将在新场景就绪后重新套用") : L"玩家或地图已变化；NPC 清理尚未完整，保留可重试状态并记录日志");
    } else if (active_ && (SkelMesh(primary_.object) != (source_driver_mode_?source_asset_.object:target_mesh_.object)|| (source_driver_mode_&&SkelMesh(display_.object)!=target_mesh_.object))) {
        if (cutscene_host_) { KeepForNextScene("cutscene-mesh-changed"); }
        const bool restored = Restore();
        Status(restored ? L"游戏更换了主网格，已停止 NPC 覆盖并清理临时设置" : L"游戏更换了主网格；已保留外部改动，部分共享设置需查看日志");
    }
    if (active_ && context_ok && !cleanup_pending_) {
        if(source_driver_mode_){RefreshForearmBindings();RefreshWeaponRoutes();QuietAimNodes(display_.object);}
        WatchSkill();HoldBodyCollision();HoldCameraLift();
        if(skill_.handle>0&&!hidden_weapons_.empty())HideHeroWeapons(); // a weapon the skill attached since
        const char* why=nullptr;
        if(!SourceLeaseValid(why)){}
        else if(source_driver_mode_&&!RepairSourceSettings())why="source-flag-repair";
        else if(!RepairOriginalShadows())why="original-part-shadow-repair";
        else if(!RefreshOwnedLighting())why="lighting-channels";
        else if(pose_session_&&InterlockedCompareExchange(&pose_session_->failed,0,0))why=pose_session_->why;
        else if(!grip_hook_)why="weapon-adapter-missing";
        else if(ue::At<uintptr_t*>(primary_.object,0)!=grip_hook_->table)why="weapon-adapter-vtable-replaced";
        else if(InterlockedCompareExchange(&grip_hook_->failures,0,0))why="weapon-adapter-query-failed";
        if (why) {
            cleanup_pending_=true;
            Logf("NPC_RETARGET retiring at next idle reason=%s",why);
            Status(L"NPC 照明通道 / 当前武器姿势校验发生变化，将于空闲时恢复原模型");
        }
        if (Object* conflict = source_driver_mode_?nullptr:ForeignSkeletonUser(context, Property<Object*>(target_mesh_.object, "Skeleton"))) {
            cleanup_pending_ = true;
            Logf("NPC_RETARGET another shared-skeleton user appeared: component=%s; retiring experiment at next idle", Text(conflict).c_str());
            Status(L"场景新增同骨架角色；NPC 外观将于空闲时恢复。后续生成同骨架角色前应先恢复主角");
        }
    }
    if (cleanup_pending_ && waiting_action_ < 0) {
        // Rollback may have failed before active_=true. Keep retrying that
        // ledger too, while avoiding a live current-avatar montage reset.
        if (!Same(primary_) || primary_restored_ || !context_ok || context.mesh != primary_.object || Idle(context)) {
            if (cutscene_host_ && active_) { CutsceneRetryFor(pawn_.object,primary_.object); CutsceneRetryFailed(); KeepForNextScene("cutscene-look-retired"); }  // back on the pawn after the cutscene
            const bool restored = Restore();
            Status(restored ? L"未完成的 NPC 临时适配已清理，可重新选择模型" : L"NPC 清理尚未完整，已保留恢复记录；新外观切换保持阻断");
        }
        Publish();
    }
    if (waiting_action_ < 0 && !cleanup_pending_ && context_ok) DriveCutsceneLook(context);
    if (waiting_action_ < 0 && resume_entry_ >= 0) ResumeAfterScene(context_ok, disabled, context);
    if (waiting_action_ < 0) return;
    if (GetTickCount64() > wait_deadline_) { waiting_action_ = -1; blink_wanted_ = false; Publish(); Status(L"等待过场结束或有效玩家超时，未执行 NPC 更换"); return; }
    if (waiting_action_ == static_cast<int32_t>(Action::Restore) && (!Same(primary_) || primary_restored_ || !context_ok || context.mesh != primary_.object)) {
        waiting_action_ = -1; blink_wanted_ = false; const bool restored = Restore();
        Status(restored ? L"NPC 临时适配已清理" : L"NPC 清理尚未完整，保留恢复记录以便重试"); Publish(); return;
    }
    const bool chain_look = borrow_.stage == Borrow::Stage::Going || borrow_.stage == Borrow::Stage::Returning;
    if (!context_ok || !LookReady(context)) {
        if (chain_look) ChainLookWaits(!context_ok ? "no-player" : "look-not-ready (cutscene / hero hidden / no anim instance / movement None)");
        Publish(true); Status(L"等待过场结束、主角重新出现，再执行 NPC 更换 / 恢复"); return;
    }
    if (waiting_action_ == static_cast<int32_t>(Action::Apply)) {
        if (!release_appearance_) {
            waiting_action_ = -1; Publish(); Fail(L"尚未接入主角外观释放适配器；检查模板可用，应用已阻断"); return;
        }
        // A chain's look change: the game's dead flag alone (health above 0) does not hold it; some boss moves cast on
        // the hero leave that flag set for seconds.
        const char* why = nullptr;
        if (!release_appearance_(chain_look, &why)) {
            if (chain_look) ChainLookWaits(why ? why : "appearance-release");
            Publish(true); Status(L"先完整释放上一种主角外观 / 发型覆盖，再应用 NPC 原模型；恢复未成功时保持等待"); return;
        }
        if (chain_look && why && borrow_.note != why) {
            borrow_.note = why;
            Logf("NPC_COMBO boss look id=%s: the game marks the hero dead with health above 0 (%s): changing anyway", LookName(waiting_entry_), why);
        }
    }
    action = waiting_action_; entry = waiting_entry_; waiting_action_ = -1;
    const bool chain_change = look_cancel_ && (borrow_.stage == Borrow::Stage::Going || borrow_.stage == Borrow::Stage::Returning); look_cancel_ = false;
    // Back to the hero's look with no move of the chain after it (borrow_.node < 0): an action the player pressed as the
    // boss move ended goes on through the change, and the chain reads it as a press once the look is back
    // (DriveHeroTriggers). Until 11-12 it was cancelled here, so the chain went on only at the player's next press
    // (user: "变身无法流畅衔接"; log 18:06-18:07: HeiMoShang_A, Tang_A_Snap and two executions cancelled at moves' ends).
    const std::string kept = chain_change && borrow_.stage == Borrow::Stage::Returning && borrow_.node < 0 ? HeroNewAction(context) : std::string();
    const bool cancel = chain_change && kept.empty();
    if (action != static_cast<int32_t>(Action::Inspect)) {
        // At once, whatever the hero is doing: a chain's change stops the hero's action first (its boss move follows).
        const HeroMotion motion = MotionOf(context);
        const bool busy = !motion.known || motion.montage || motion.speed > 10 || (motion.mode != 1 && motion.mode != 2);
        if (busy || cancel) {
            const std::string cancelled = cancel ? CancelHeroActions(context) : kept.empty() ? std::string("none (kept)") :
                "none (kept the hero's new " + kept + ": a press, read once the look is back)";
            Logf("NPC_COMBO boss look immediate cancelled=%s state=%s action=%s look=%s", cancelled.c_str(), MotionText(motion).c_str(),
                 action == static_cast<int32_t>(Action::Apply) ? "apply" : "restore", LookName(entry));
        }
    }
    const bool blink = blink_wanted_ && action != static_cast<int32_t>(Action::Inspect) && !Auditing(); blink_wanted_ = false;
    if (blink) BlinkOut(context.pawn);
    struct BlinkEnd { bool on; ~BlinkEnd() { if (on) BlinkDone(); } } blink_end{blink};
    if (action == static_cast<int32_t>(Action::Restore)) { const bool restored = Restore(); Status(restored ? L"已恢复原主角模型、材质与临时骨架 / 挂点设置" : L"恢复尚未完整：部分对象、动画任务或共享设置已变化，请查看日志后重试"); }
    else if (entry >= 0 && static_cast<size_t>(entry) < std::size(kProfiles)) {
        failure_text_.clear(); Publish(true);
        const bool ok = action == static_cast<int32_t>(Action::Inspect) ? Inspect(entry) : Apply(context, entry);
        if (!ok) {
            Logf("NPC_RETARGET apply failed; rollback attempted");
            if (!failure_text_.empty()) Status(L"NPC 重定向未完成：%s", failure_text_.c_str());
            else Status(L"NPC 重定向未完成，已尝试恢复原模型；详见 NPC_RETARGET 日志");
        }
        Publish();
    }
}
int FaultFilter(const EXCEPTION_POINTERS* exception) {
    fault_code_ = exception->ExceptionRecord->ExceptionCode; fault_address_ = reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress); return EXCEPTION_EXECUTE_HANDLER;
}
void Recover() { __try { pool_enabled_=false;Restore();PoolClear(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
void GuardedTick() {
    __try { Tick(); }
    __except (FaultFilter(GetExceptionInformation())) {
        ++faults_; waiting_action_ = -1;
        Logf("NPC_RETARGET fault %lu/3 code=0x%08lx address=%p stage=%s", faults_, fault_code_, reinterpret_cast<void*>(fault_address_), stage_);
        Recover(); if (faults_ >= 3) InterlockedExchange(&disabled_, 1);
        Status(L"NPC 重定向错误 0x%08lx，已停止并尝试恢复；Boss Rush 与原调试继续运行", fault_code_); Publish();
    }
}
}

void Initialize(Logger logger, ReleaseAppearance release_appearance) {
    logger_ = logger; release_appearance_ = release_appearance;
    InterlockedExchange(&perf_enabled_,0);Snapshot state;
    NpcCombo::SetLogger(logger); NpcCombo::StartWatcher();  // .combo files: migration, live reload
    {  // 派生插件 (off until the page's 派生插件 is on: nothing loads before)
        ComboPlugins::Bridge bridge;
        bridge.config=&PluginConfig;bridge.state=&PluginState;bridge.hero=&PluginHero;bridge.press=&PluginPress;bridge.reset=&PluginReset;
        bridge.cancel=&PluginCancel;bridge.watched=&PluginWatched;bridge.trigger=&PluginTrigger;bridge.play=&PluginPlay;
        ComboPlugins::Initialize(bridge,&PluginLog,&PluginNotice);
    }
    state.appearance_gate = release_appearance_ != nullptr;
    for (size_t i = 0; i < std::size(kProfiles); ++i) state.entries.push_back({static_cast<int32_t>(i), kProfiles[i].id, kProfiles[i].name, kProfiles[i].detail});
    AcquireSRWLockExclusive(&snapshot_lock); snapshot_ = std::move(state); ReleaseSRWLockExclusive(&snapshot_lock);
    const float damage = SettingsStore::GetFloat(kSkillSection, L"DamageMultiplier", kSkillDamageDefault);
    skill_damage_.store(std::isfinite(damage) ? std::clamp(damage, kSkillDamageMin, kSkillDamageMax) : kSkillDamageDefault, std::memory_order_relaxed);
    boss_loco_.store(SettingsStore::GetBool(kRetargetSection, L"BossLocomotion", false), std::memory_order_relaxed);
    initialized_ = true; Logf("NPC_RETARGET initialized profiles=%zu native AnimationRelative skillDamage=x%.2f bossLocomotion=%d", std::size(kProfiles), skill_damage_.load(std::memory_order_relaxed), boss_loco_.load(std::memory_order_relaxed) ? 1 : 0);
}
bool Request(Action action, int32_t entry) {
    if (action == Action::Apply && !InterlockedCompareExchange(&experimental_, 0, 0)) return false;
    if ((InterlockedCompareExchange(&disabled_, 0, 0) && action != Action::Restore) || ((action == Action::Apply || action == Action::Inspect) && (entry < 0 || static_cast<size_t>(entry) >= std::size(kProfiles)))) return false;
    AcquireSRWLockExclusive(&request_lock); pending_action_ = static_cast<int32_t>(action); pending_entry_ = entry; ReleaseSRWLockExclusive(&request_lock);
    if (action != Action::Inspect) InterlockedExchange(&forget_resume_, 1); // the game thread drops the remembered skin
    Status(L"NPC 重定向请求已排队；更换前会检查过场状态、部件与武器挂点"); return true;
}
bool SetExperimental(bool enabled) {
    if (enabled && InterlockedCompareExchange(&disabled_, 0, 0)) return false;
    InterlockedExchange(&experimental_, enabled ? 1 : 0);
    AcquireSRWLockExclusive(&snapshot_lock); snapshot_.experimental = enabled; ReleaseSRWLockExclusive(&snapshot_lock);
    if (!enabled) return Request(Action::Restore);
    Status(L"NPC 外观切换已打开；仍需通过场景、外观互斥、骨架与挂点校验"); return true;
}
float SkillDamageMultiplier() { return skill_damage_.load(std::memory_order_relaxed); }
bool SetSkillDamageMultiplier(float multiplier) {
    if (!std::isfinite(multiplier)) return false;
    const float value = std::clamp(multiplier, kSkillDamageMin, kSkillDamageMax);
    if (value == skill_damage_.exchange(value, std::memory_order_relaxed)) return true;
    SettingsStore::PutFloat(kSkillSection, L"DamageMultiplier", value); Logf("NPC_SKILL damage multiplier x%.2f", value); return true;
}
bool BossLocomotion() { return boss_loco_.load(std::memory_order_relaxed); }
void SetBossLocomotion(bool enabled) {
    if (enabled == boss_loco_.exchange(enabled, std::memory_order_relaxed)) return;
    SettingsStore::PutBool(kRetargetSection, L"BossLocomotion", enabled); Logf("NPC_LOCO boss locomotion option %s", enabled ? "on" : "off");
}
// The NPC skill key, with or without a look: a look's chain / random round, or the hero's own chain (its 按键 node).
bool CancelAbilitySpec(ue::Object* asc, int32_t handle) { return CancelSpec(asc, handle); }
bool RequestSkill() {
    if (InterlockedCompareExchange(&disabled_, 0, 0)) return false;
    InterlockedExchange(&skill_request_pad_, static_cast<LONG>(UiHotkeys::CurrentPad().buttons));  // the chord that fired it
    InterlockedExchange(&skill_request_, 1); return true;
}
unsigned char* GrabDisplay(double& size) {
    const bool on = active_ && source_driver_mode_ && Same(display_);
    size = on ? skin_size_ : 1.0; return on ? display_.object : nullptr;
}
Snapshot GetSnapshot() { AcquireSRWLockShared(&snapshot_lock); Snapshot copy = snapshot_; ReleaseSRWLockShared(&snapshot_lock); return copy; }
bool GetSkinMoves(int32_t skin, SkinMoves& moves) {
    moves = {};
    if (skin == -1) {
        // The hero's own chain: its abilities as the game thread last read them.
        moves.profile = NpcCombo::kHeroProfile; moves.name = L"主角";
        AcquireSRWLockShared(&hero_moves_lock_); moves.skills = hero_moves_; ReleaseSRWLockShared(&hero_moves_lock_);
        return true;
    }
    if (skin < 0 || static_cast<size_t>(skin) >= std::size(kProfiles)) return false;
    moves.profile = kProfiles[skin].id; moves.name = kProfiles[skin].name;
    std::set<std::string> seen;
    const NpcDodgeSet& dodges = kNpcDodgeSets[skin];
    for (size_t i = 0; i < dodges.count; ++i)
        if (seen.insert(NpcCombo::ShortName(dodges.dodges[i].ability)).second) { moves.dodges.push_back(NpcCombo::ShortName(dodges.dodges[i].ability)); moves.directions.push_back(dodges.dodges[i].dir); }
    const NpcSkillSet& set = kNpcSkillSets[skin];
    for (size_t i = 0; i < set.skill_count; ++i)  // not the moves that kill their caster (SelfKillMove)
        if (!SelfKillMove(set.skills[i]) && seen.insert(NpcCombo::ShortName(set.skills[i])).second) moves.skills.push_back(NpcCombo::ShortName(set.skills[i]));
    return true;
}
std::wstring MoveLabel(const std::string& ability) { return SkillLabel(ability); }
bool MovePreviewSubject(int32_t skin, const std::string& ability, std::string& monster, std::string& package) {
    monster.clear(); package.clear();
    if (skin < 0 || static_cast<size_t>(skin) >= std::size(kNpcSkillSets) || !kNpcSkillSets[skin].monster || !*kNpcSkillSets[skin].monster) return false;
    package = MovePackage(static_cast<size_t>(skin), ability);
    if (package.empty()) return false;
    monster = kNpcSkillSets[skin].monster;
    return true;
}
// Characters two paths share up to their last common '/', ignoring case.
static size_t SharedFolder(const std::string& a, const char* b) {
    if (!b) return 0;
    size_t folder = 0;
    for (size_t i = 0; i < a.size() && b[i]; ++i) {
        const char x = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i]))), y = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (x != y) break;
        if (x == '/') folder = i + 1;
    }
    return folder;
}
bool GrabPreviewMonster(const std::wstring& boss_id, const std::wstring& source, std::string& monster) {
    monster.clear();
    const int32_t row = SkinForGrab(boss_id, source);
    if (row < 0 || static_cast<size_t>(row) >= std::size(kNpcSkillSets) || !kNpcSkillSets[row].monster || !*kNpcSkillSets[row].monster) return false;
    monster = kNpcSkillSets[row].monster;
    return true;
}
int32_t SkinForGrab(const std::wstring& boss_id, const std::wstring& source) {
    const std::string key = GrabBoss::Key(boss_id), path = Utf8(source.c_str()), name = Utf8(boss_id.c_str());
    if (key.empty()) return -1;
    int32_t best = -1; size_t best_folder = 0; bool best_named = false;
    for (size_t row = 0; row < std::size(kProfiles); ++row) {
        if (GrabBoss::Key(std::string_view(kProfiles[row].id)) != key) continue;
        const NpcSkillSet& set = kNpcSkillSets[row];
        size_t folder = SharedFolder(path, set.monster);
        for (size_t i = 0; i < set.skill_count; ++i) folder = (std::max)(folder, SharedFolder(path, set.skills[i]));
        const bool named = _stricmp(kProfiles[row].id, name.c_str()) == 0;
        if (best < 0 || folder > best_folder || (folder == best_folder && named && !best_named)) { best = static_cast<int32_t>(row); best_folder = folder; best_named = named; }
    }
    return best;
}
ue::Object* BoneBody(){if(active_&&Same(display_))return display_.object;Context context;return ContextNow(context)?context.mesh:nullptr;}
BoneMotion::Config BoneEdits(){
    if(combo_.trigger<0||combo_.playing<0||combo_.phase==ComboRun::Phase::Idle)return {};
    const int mod=NpcCombo::ModFor(combo_.config,combo_.trigger,combo_.playing,NpcCombo::kModBone);
    return mod>=0?combo_.config.chain[static_cast<size_t>(mod)].bones:BoneMotion::Config{};
}
void OnGameThreadGuarded() { GuardedTick(); }
bool BeforeTravel() {
    AcquireSRWLockExclusive(&request_lock); pending_action_ = -1; ReleaseSRWLockExclusive(&request_lock);
    __try { KeepForNextScene("travel"); waiting_action_ = -1; const bool restored=Restore();return PoolClear()&&restored; }
    __except (FaultFilter(GetExceptionInformation())) { Logf("NPC_RETARGET pre-travel cleanup fault 0x%08lx", fault_code_); return false; }
}
bool ReleaseForExternal() {
    AcquireSRWLockExclusive(&request_lock); pending_action_ = -1; ReleaseSRWLockExclusive(&request_lock);
    __try {
        waiting_action_ = -1;
        if (!HasResources()) { const bool cleared=PoolClear();Publish();return cleared; }
        ForgetResume("hero outfit applied");
        Context context;
        if (!primary_restored_ && Same(primary_) && ContextNow(context) && context.mesh == primary_.object && !Idle(context)) {
            Status(L"先等待当前 NPC 模型空闲，再恢复并切换到主角套装"); Publish(true); return false;
        }
        const bool restored = Restore();const bool cleared=PoolClear(); Publish(); return restored&&cleared;
    } __except (FaultFilter(GetExceptionInformation())) {
        cleanup_pending_ = HasResources(); Publish();
        Logf("NPC_RETARGET appearance-release fault 0x%08lx; preserved cleanup ledger", fault_code_); return false;
    }
}
bool OwnsAppearance() { return active_ || HasResources(); }
void ShownWithHero(std::vector<Object*>& out) {
    for (const GrabBody& g : grab_bodies_) {
        if (Same(g.actor)) out.push_back(g.actor.object);
        if (Same(g.victim)) out.push_back(g.victim.object);
    }
}
namespace {
bool BeforeActorClassSpawnImpl(Object* actor_class, bool release = true) {
    std::vector<Object*> modified;
    const auto add_skeleton = [&](Identity identity) {
        if (Same(identity) && std::find(modified.begin(), modified.end(), identity.object) == modified.end()) modified.push_back(identity.object);
    };
    add_skeleton(skeleton_edit_.skeleton);
    for (const SocketAdded& socket : sockets_) add_skeleton(socket.skeleton);
    if (modified.empty()) return true;
    const auto retire = [&](const char* reason) {
        if (!release) { Logf("NPC_RETARGET shared Skeleton check class=%s: %s (nothing released)", ue::Alive(actor_class) ? Text(actor_class).c_str() : "invalid", reason); return false; }
        Logf("NPC_RETARGET pre-spawn release class=%s reason=%s", ue::Alive(actor_class) ? Text(actor_class).c_str() : "invalid", reason);
        const bool released = ReleaseForExternal();
        if (!released) Status(L"生成前需恢复共享骨架：等待当前 NPC 模型空闲并完整清理，尚未生成角色");
        return released;
    };
    if (!ue::Alive(actor_class) || !ue::IsA(actor_class, Class("/Script/CoreUObject.Class"))) return retire("invalid/unresolved actor class");
    std::vector<Object*> lineage, defaults, inspected;
    for (Object* type = actor_class; type; type = ue::At<Object*>(type, ue::layout::kSuper)) {
        if (!ue::Alive(type) || lineage.size() >= 64 || std::find(lineage.begin(), lineage.end(), type) != lineage.end()) return retire("unresolved class ancestry");
        lineage.push_back(type); defaults.push_back(nullptr);
    }
    ue::ForEachObject([&](Object* object) {
        if (!ue::IsDefault(object) || !ue::Alive(object)) return true;
        const auto owner = std::find(lineage.begin(), lineage.end(), ue::ClassOf(object));
        if (owner != lineage.end()) defaults[static_cast<size_t>(owner - lineage.begin())] = object;
        return true;
    });
    if (defaults.empty() || !defaults.front()) return retire("class default object not loaded");
    bool unknown = false, intersects = false;
    Object* skinned_class = Class("/Script/Engine.SkinnedMeshComponent"), *skeletal_class = Class("/Script/Engine.SkeletalMeshComponent");
    if (!skinned_class || !skeletal_class) return retire("skinned component metadata unavailable");
    const auto inspect = [&](Object* component) {
        if (!component) { unknown = true; return; }
        if (!ue::Alive(component)) { unknown = true; return; }
        if (!ue::IsA(component, skinned_class) || std::find(inspected.begin(), inspected.end(), component) != inspected.end()) return;
        inspected.push_back(component);
        Object* asset = ue::IsA(component, skeletal_class) ? SkelMesh(component) : Property<Object*>(component, "SkinnedAsset");
        if (!ue::Alive(asset)) { unknown = true; return; }
        const int32_t skeleton_offset = Offset(asset, "Skeleton");
        Object* skeleton = skeleton_offset >= 0 ? Property<Object*>(asset, "Skeleton") : nullptr;
        if (!ue::Alive(skeleton)) { unknown = true; return; }
        if (std::find(modified.begin(), modified.end(), skeleton) != modified.end()) {
            intersects = true;
            Logf("NPC_RETARGET pre-spawn shared Skeleton intersection component=%s asset=%s skeleton=%s", Text(component).c_str(), Text(asset).c_str(), Text(skeleton).c_str());
        }
    };
    // Only the concrete CDO is an effective actor default. Its component list
    // includes inherited native default subobjects. Do not reinterpret empty
    // native-base CDO meshes as extra components spawned by the concrete class.
    Call get(defaults.front(), "K2_GetComponentsByClass");
    if (!get.Set("ComponentClass", skinned_class).Invoke()) unknown = true;
    else {
        const Array components = get.Get<Array>("ReturnValue");
        if (components.num < 0 || components.num > 2048 || components.max < components.num || (components.num && !components.data)) unknown = true;
        else for (int32_t i = 0; i < components.num; ++i) inspect(static_cast<Object**>(components.data)[i]);
        ue::Free(components.data);
    }
    if (Field(ue::ClassOf(defaults.front()), "Mesh")) inspect(Property<Object*>(defaults.front(), "Mesh"));
    ue::ForEachObject([&](Object* component) {
        if (!ue::IsTemplate(component) || !ue::Alive(component) || !ue::IsA(component, skinned_class)) return true;
        for (size_t i = 0; i < lineage.size(); ++i)
            if (SourceOwner(component, lineage[i], i == 0 ? defaults.front() : nullptr)) { inspect(component); break; }
        return true;
    });
    for (Object* type : lineage) {
        Object* scs = Property<Object*>(type, "SimpleConstructionScript");
        if (!scs) continue;
        if (!ue::Alive(scs)) { unknown = true; continue; }
        const Array nodes = Property<Array>(scs, "AllNodes");
        if (nodes.num < 0 || nodes.num > 2048 || nodes.max < nodes.num || (nodes.num && !nodes.data)) { unknown = true; continue; }
        for (int32_t i = 0; i < nodes.num; ++i) {
            Object* node = static_cast<Object**>(nodes.data)[i];
            if (!ue::Alive(node)) { unknown = true; continue; }
            inspect(Property<Object*>(node, "ComponentTemplate"));
        }
    }
    if (intersects) return retire("authored class uses an owned modified Skeleton");
    if (unknown) return retire("unresolved skinned template asset/Skeleton");
    Logf("NPC_RETARGET pre-spawn class=%s authored skinned templates=%zu disjoint; keeping current NPC model", Text(actor_class).c_str(), inspected.size());
    return true;
}
}
bool ClassSharesEditedSkeleton(Object* actor_class) {
    __try { return !BeforeActorClassSpawnImpl(actor_class, false); }
    __except (FaultFilter(GetExceptionInformation())) { Logf("NPC_RETARGET shared Skeleton check fault 0x%08lx", fault_code_); return true; }
}
bool HeroLockTarget(unsigned char (&target)[16], Object*& enemy) {
    enemy = nullptr; std::memset(target, 0, sizeof(target));
    Context context; if (!ContextNow(context)) return false;
    Object* targeting = TargetingComponent(context.pawn); if (!targeting) return false;
    static const Name soft = ue::MakeName(L"Targeting.Handler.CombatSoftLock"); if (soft.None()) return false;
    Call find(targeting, "FindTargetByHandler");
    if (find.Set("HandlerTag", soft).Set("UseLockTarget", HasLock(targeting)).Invoke() && find.Get<bool>("ReturnValue")) {
        const LockTarget found = find.Get<LockTarget>("Target"); std::memcpy(target, found.bytes, sizeof(target));
        int32_t at = -1; enemy = EnemyIn(target, static_cast<int32_t>(sizeof(target)), context.pawn, at);
        if (enemy) return true;
        std::memset(target, 0, sizeof(target));
    }
    // No lock, or no living enemy in it: the nearest hostile within kAssistEnemyCm of the hero (no lock bytes).
    constexpr double kAssistEnemyCm = 3000;
    Vector where{}; enemy = NearestEnemyPawn(context.pawn, kAssistEnemyCm, where);
    return false;
}
bool BeforeActorClassSpawn(Object* actor_class) {
    __try { return BeforeActorClassSpawnImpl(actor_class); }
    __except (FaultFilter(GetExceptionInformation())) {
        Logf("NPC_RETARGET pre-spawn inspection fault 0x%08lx; requiring conservative appearance release", fault_code_);
        return ReleaseForExternal();
    }
}
bool HandleCommand(const char* verb, const char* arguments) {
    if(!std::strcmp(verb,"npcperf")){std::istringstream input(arguments?arguments:"");std::string value;input>>value;if(value!="on"&&value!="off")return false;return SetPerfEnabled(value=="on");}
    if (std::strcmp(verb, "npcretarget")) return false;
    std::istringstream input(arguments ? arguments : ""); std::string action, id; input >> action >> id;
    if(action=="perf"){if(id!="on"&&id!="off")return false;return SetPerfEnabled(id=="on");}
    if (action == "experimental") {
        if (id != "on" && id != "1" && id != "off" && id != "0") return false;
        return SetExperimental(id == "on" || id == "1");
    }
    if (action == "restore") return Request(Action::Restore);
    if (action == "bare") {  // npcretarget bare <skin index> <skill, 1-based>: that move cast as a 不变身 BOSS 技能 (no look)
        char* end = nullptr; const long skin = std::strtol(id.c_str(), &end, 10); long skill = 0; input >> skill;
        if (id.empty() || !end || *end || skin < 0 || static_cast<size_t>(skin) >= std::size(kNpcSkillSets) || skill < 1 || static_cast<size_t>(skill) > kNpcSkillSets[skin].skill_count) return false;
        InterlockedExchange(&bare_test_, ((skin + 1) << 16) | skill); return true;
    }
    if (action == "press") {  // npcretarget press <bits hex> [hold <ms>]: a hero-action press, its button held that long
        const unsigned long bits = std::strtoul(id.c_str(), nullptr, 16); if (!bits) return false;
        std::string word; long ms = 0; if (input >> word) { if (word != "hold" || !(input >> ms) || ms < 0 || ms > 20000) return false; }
        InterlockedExchange(&press_test_hold_, ms); InterlockedExchange(&press_test_, static_cast<LONG>(bits)); return true;
    }
    if (action == "resources") { InterlockedExchange(&resource_probe_, 1); return true; }
    if (action == "lockstruct") { InterlockedExchange(&lock_struct_probe_, 1); return true; }  // tests: the lock target struct
    if (action == "camoffset") {  // tests: npcretarget camoffset <x> <y> <z> | off: the 视角跟随 additive by hand
        if (id == "off") { InterlockedExchange(&cam_offset_test_, 2); return true; }
        double y = 0, z = 0; input >> y >> z; cam_offset_value_ = Vector{std::strtod(id.c_str(), nullptr), y, z}; InterlockedExchange(&cam_offset_test_, 1); return true;
    }
    if (action == "viewfollow") {  // npcretarget viewfollow on | off | world | local
        if (id == "on" || id == "off") view_enabled_ = id == "on"; else if (id == "world" || id == "local") view_space_ = id == "local" ? 1 : 0; else return false;
        Logf("NPC_VIEW follow %s, offsets in %s axes", view_enabled_ ? "on" : "off", view_space_ ? "the control yaw's" : "world"); return true;
    }
    if (action == "viewprobe") {  // tests: npcretarget viewprobe <ms>: model / actor / camera every 200 ms
        const long ms = std::strtol(id.c_str(), nullptr, 10); if (ms <= 0 || ms > 60000) return false;
        InterlockedExchange(&view_probe_ms_, ms); return true;
    }
    if (action == "trimprobe") {  // tests: npcretarget trimprobe <frames> (0: off): the trimmed montage's state each frame after its jump
        const long frames = std::strtol(id.c_str(), nullptr, 10); if (frames < 0 || frames > 600) return false;
        InterlockedExchange(&trim_probe_frames_, frames); return true;
    }
    if (action == "airlift" || action == "airfoe") {
        // tests: npcretarget airlift|airfoe <cm> [hang %] [seconds]: the hero / the nearest enemy thrown up and held
        double hang = 0, seconds = 1; input >> hang; if (input.fail()) hang = 0; input.clear(); input >> seconds; if (input.fail()) seconds = 1;
        air_test_cm_ = std::clamp(std::strtod(id.c_str(), nullptr), 0.0, 1500.0); air_test_hang_ = std::clamp(hang, 0.0, 100.0); air_test_s_ = std::clamp(seconds, 0.0, 30.0);
        InterlockedExchange(&air_test_, action == "airlift" ? 1 : 2); return true;
    }
    if (action == "jump") { InterlockedExchange(&air_test_, 3); return true; }  // tests: the hero jumps (ACharacter::Jump)
    if (action == "ownskill") {  // tests: npcretarget ownskill <bits hex>: the hero's own ability of those action bits (8 影之武装, 20 战技)
        const unsigned long bits = std::strtoul(id.c_str(), nullptr, 16); if (!bits || bits > 0x7fffffff) return false;
        InterlockedExchange(&own_skill_test_, static_cast<LONG>(bits)); return true;
    }
    if (action == "airprobe") {  // tests: npcretarget airprobe <frames 0-600>: the chain's next lifts logged frame by frame
        char* end = nullptr; const long frames = std::strtol(id.c_str(), &end, 10); if (id.empty() || !end || *end || frames < 0 || frames > 600) return false;
        InterlockedExchange(&air_probe_frames_, frames); Logf("NPC_TEST airprobe %ld frames", frames); return true;
    }
    if (action == "aimprobe") {  // tests: npcretarget aimprobe [pitch]: the hook's aim at 12 headings (nothing is cast)
        aim_test_pitch_ = id.empty() ? 0 : std::strtod(id.c_str(), nullptr); InterlockedExchange(&aim_test_, 2); return true;
    }
    if (action == "aim") {  // tests: npcretarget aim <pitch> [yaw offset]: the player's view, from the hero's facing
        double yaw = 0; input >> yaw; if (input.fail()) yaw = 0;
        aim_test_pitch_ = std::strtod(id.c_str(), nullptr); aim_test_yaw_ = yaw; InterlockedExchange(&aim_test_, 1); return true;
    }
    if (action == "actiondump") { InterlockedExchange(&action_dump_, 1); return true; }  // tests: the running skill's live actions
    if (action == "timelinewatch") {  // tests: npcretarget timelinewatch <ms>: the running skill's timeline tasks as they change
        const long ms = std::strtol(id.c_str(), nullptr, 10); if (ms <= 0 || ms > 60000) return false;
        InterlockedExchange(&timeline_watch_ms_, ms); return true;
    }
    if (action == "abilityprobe") {  // tests: npcretarget abilityprobe <frames>: what of the running skill's ability moves as it plays
        const long frames = std::strtol(id.c_str(), nullptr, 10); if (frames <= 0 || frames > 600) return false;
        InterlockedExchange(&ability_probe_frames_, frames); return true;
    }
    if (action == "tagdump") {  // tests: npcretarget tagdump <ms>: the 钩子 target's gameplay tags as they change
        const long ms = std::strtol(id.c_str(), nullptr, 10); if (ms <= 0 || ms > 120000) return false;
        InterlockedExchange(&tag_dump_ms_, ms); return true;
    }
    if (action == "trimjump") {  // tests: npcretarget trimjump now|tick|replay|clock (how a 裁剪 start is reached)
        const LONG mode = id == "now" ? 0 : id == "tick" ? 1 : id == "replay" ? 2 : id == "clock" ? 3 : -1; if (mode < 0) return false;
        InterlockedExchange(&trim_jump_mode_, mode); Logf("NPC_COMBO trim start: %s", TrimJumpName(mode)); return true;
    }
    if (action == "testchain") {  // npcretarget testchain <Encode text> | off
        if (id != "off" && (id.size() < 2 || id[0] != 'E')) return false;
        AcquireSRWLockExclusive(&test_chain_lock_); test_chain_text_ = id == "off" ? std::string() : id; ReleaseSRWLockExclusive(&test_chain_lock_);
        InterlockedIncrement(&test_chain_serial_); return true;
    }
    if (action == "skill") {  // npcretarget skill [hold <ms>]: the NPC key, held that long
        long ms = 0; if (id == "hold") { if (!(input >> ms) || ms < 0 || ms > 20000) return false; } else if (!id.empty()) return false;
        test_hold_until_ = GetTickCount64() + static_cast<ULONGLONG>(ms); return RequestSkill();
    }
    if (action == "cast") { char* end = nullptr; const long pick = std::strtol(id.c_str(), &end, 10); if (id.empty() || !end || *end || pick < 1 || pick > 256) return false; InterlockedExchange(&cast_request_, pick); return true; }
    if (action == "audit") {
        // npcretarget audit start [first skin index] [first skill, 1-based] [end skin index, exclusive] | list | export | stop
        AuditRequest request; size_t from = 0, skill = 1, end = std::size(kNpcSkillSets);
        if (id == "start") { input >> from; if (input.fail()) { input.clear(); from = 0; } else { input >> skill; if (input.fail()) { input.clear(); skill = 1; } else { input >> end; if (input.fail()) end = std::size(kNpcSkillSets); } }
            if (from >= std::size(kNpcSkillSets) || !skill) return false; request = {1, from, skill - 1, end}; }
        else if (id == "list") {
            // npcretarget audit list <skin index> <skill,skill,...> (1-based): only those skills of that skin.
            std::string skills; input >> from >> skills;
            if (input.fail() || from >= std::size(kNpcSkillSets)) return false;
            std::vector<size_t> only; std::stringstream parts(skills);
            for (std::string part; std::getline(parts, part, ',');) { const long n = std::strtol(part.c_str(), nullptr, 10); if (n >= 1 && n <= 512) only.push_back(static_cast<size_t>(n - 1)); }
            if (only.empty()) return false;
            std::sort(only.begin(), only.end()); only.erase(std::unique(only.begin(), only.end()), only.end());
            request = {1, from, only.front(), from + 1, only}; }
        else if (id == "export") {
            // npcretarget audit export [first skin index] [end skin index, exclusive]: no casts, data only (PBZAuditExport.jsonl).
            input >> from; if (input.fail()) { input.clear(); from = 0; } else { input >> end; if (input.fail()) end = std::size(kNpcSkillSets); }
            if (from >= std::size(kNpcSkillSets)) return false; request = {4, from, 0, end}; }
        else if (id == "stop") request.action = 2;
        else if (id == "grabs") { size_t first = 0; input >> first; if (input.fail()) first = 0; request = {3, first, 0, 0}; }
        else if (id == "speed") {
            // npcretarget audit speed <x>: 1 = normal, up to 10x. Lives until the audit ends (the original
            // world time dilation is restored there), and applies from the next skill on.
            float speed = 0.0f; input >> speed;
            if (input.fail() || !(speed >= 1.0f) || speed > 10.0f) { Logf("NPC_AUDIT speed needs 1..10"); return true; }
            audit_speed_ = speed;
            Logf("NPC_AUDIT speed %.2fx (from the next skill; restored when the audit ends)", speed);
            return true; }
        else return false;
        AcquireSRWLockExclusive(&request_lock); audit_request_ = request; ReleaseSRWLockExclusive(&request_lock); return true;
    }
    if (action == "dummy") {  // the page's 测试木桩; "far" (tests): 10 m out
        if (id != "on" && id != "off" && id != "far") return false; TestDummy::Request(id != "off", id == "far" ? 1000.0 : 350.0); return true; }
    if (action == "preview") {
        // npcretarget preview <skin index> <skill, 1-based> | off | dump <file.bmp> | source <n> | cull <0/1> | fx: the 招式预览
        // stage without the page (source / cull / fx: MovePreview::Debug).
        if (id == "off") { MovePreview::Hide(); return true; }
        if (id == "source" || id == "cull" || id == "fx" || id == "arm") { int value = 0; input >> value; MovePreview::Debug(id, value); return true; }  // tests
        if (id == "dump") {
            std::string path; input >> path; MovePreview::Frame frame;
            if (path.empty() || !MovePreview::Latest(frame, 0) || frame.pixels.size() != static_cast<size_t>(MovePreview::kWidth) * MovePreview::kHeight) {
                Logf("MOVE_PREVIEW dump: no frame yet (status: %s)", frame.status.empty() ? "-" : "set"); return false; }
            BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
            info.biSize = sizeof(info); info.biWidth = MovePreview::kWidth; info.biHeight = -MovePreview::kHeight; info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
            const DWORD bytes = static_cast<DWORD>(frame.pixels.size() * 4);
            file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info); file.bfSize = file.bfOffBits + bytes;
            HANDLE out = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out == INVALID_HANDLE_VALUE) return false;
            DWORD written = 0; bool ok = WriteFile(out, &file, sizeof(file), &written, nullptr) && WriteFile(out, &info, sizeof(info), &written, nullptr) &&
                                         WriteFile(out, frame.pixels.data(), bytes, &written, nullptr);
            CloseHandle(out); Logf("MOVE_PREVIEW dump %s ok=%d serial=%llu", path.c_str(), ok ? 1 : 0, static_cast<unsigned long long>(frame.serial)); return ok;
        }
        char* end = nullptr; const long skin = std::strtol(id.c_str(), &end, 10); long skill = 0; input >> skill;
        if (id.empty() || !end || *end || skin < 0 || static_cast<size_t>(skin) >= std::size(kNpcSkillSets)) return false;
        const NpcSkillSet& set = kNpcSkillSets[static_cast<size_t>(skin)];
        if (!set.monster || !*set.monster || skill < 1 || static_cast<size_t>(skill) > set.skill_count) return false;
        MovePreview::Show(set.monster, set.skills[static_cast<size_t>(skill - 1)]); return true;
    }
    if (action == "assist") {
        // npcretarget assist <skin index> <skill, 1-based> [near] | stop: BOSS 援护 probe (that skin's boss casts the skill).
        if (id == "stop") { BossAssist::Stop(); return true; }
        char* end = nullptr; const long skin = std::strtol(id.c_str(), &end, 10); long skill = 0; std::string mode; input >> skill >> mode;
        if (id.empty() || !end || *end || skin < 0 || static_cast<size_t>(skin) >= std::size(kNpcSkillSets)) return false;
        const NpcSkillSet& set = kNpcSkillSets[static_cast<size_t>(skin)];
        if (!set.monster || !*set.monster || skill < 1 || static_cast<size_t>(skill) > set.skill_count) return false;
        BossAssist::Request(set.monster, set.skills[static_cast<size_t>(skill - 1)], BossAssist::Mods{}, mode == "near"); return true;
    }
    if (action == "loco") {  // npcretarget loco on|off|status: the boss locomotion option (BOSS 自带移动动画); status logs the puppet
        if (id == "on" || id == "off") { SetBossLocomotion(id == "on"); return true; }
        if (id == "status") { InterlockedExchange(&loco_probe_, 1); return true; }
        return false;
    }
    if (action == "boneprobe") {  // tests: npcretarget boneprobe <bone> [bone...]: display bone heights over the feet
        std::vector<std::string> names; for (std::string name = id; !name.empty() && names.size() < 16; name.clear(), input >> name) names.push_back(name);
        if (names.empty()) return false;
        AcquireSRWLockExclusive(&bone_probe_lock_); bone_probe_ = std::move(names); ReleaseSRWLockExclusive(&bone_probe_lock_); return true;
    }
    if (action == "damage") { char* end = nullptr; const float value = std::strtof(id.c_str(), &end); return !id.empty() && end && !*end && SetSkillDamageMultiplier(value); }
    if (action == "status") { const Snapshot state = GetSnapshot(); Logf("NPC_RETARGET_STATUS %s", Utf8(state.status.c_str()).c_str()); return true; }
    if (action == "list") { for (size_t i = 0; i < std::size(kProfiles); ++i) Logf("NPC_RETARGET_PROFILE %zu id=%s name=%s", i, kProfiles[i].id, Utf8(kProfiles[i].name).c_str()); return true; }
    if (action == "apply" || action == "inspect") for (size_t i = 0; i < std::size(kProfiles); ++i)
        if (Lower(id) == Lower(kProfiles[i].id) || id == std::to_string(i)) return Request(action == "apply" ? Action::Apply : Action::Inspect, static_cast<int32_t>(i));
    return false;
}
}
