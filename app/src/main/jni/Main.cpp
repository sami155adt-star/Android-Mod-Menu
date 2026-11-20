// FFI offline game - LGL Mod Menu v5.0 এর জন্য Main.cpp
// ক্লাস/মেথডের নাম dump.cs থেকে নেওয়া। ফাংশন নাম দিয়ে খোঁজা হয় (অফসেট লাগে না)।
// "CHECK:" লেখা জায়গাগুলো ফোনে টেস্ট করে দেখতে হবে।
//
// গেমের "গ্লোবাল" = Gloo Wall (PlayerController.WallCount / AddGlooWalls)
// লবির Coin/Diamond = MenuController.CoinTransation / DimondTransation (লবিতে কাজ করবে)
// ম্যাচের টাকা = GameManager.AddMoney

#include <list>
#include <vector>
#include <cstring>
#include <pthread.h>
#include <thread>
#include <string>
#include <jni.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <dlfcn.h>
#include <mutex>
#include <functional>
#include <cstdlib>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.hpp"
#include "Menu/Menu.hpp"
#include "Menu/Jni.hpp"
#include "Includes/Macros.h"
#include "dobby.h"

#define targetLibName OBFUSCATE("libil2cpp.so")

// ---------- IL2CPP API (libil2cpp.so এক্সপোর্ট করে) ----------
typedef void*  (*t_void)();
typedef void** (*t_asms)(void*, size_t*);
typedef void*  (*t_p1)(void*);
typedef void*  (*t_cls)(void*, const char*, const char*);
typedef void*  (*t_meth)(void*, const char*, int);
typedef void*  (*t_field)(void*, const char*);
typedef void   (*t_sget)(void*, void*);
static t_void  il2_domain_get;
static t_asms  il2_domain_get_assemblies;
static t_p1    il2_assembly_get_image, il2_thread_attach, il2_thread_detach, il2_class_get_type, il2_type_get_object;
static t_cls   il2_class_from_name;
static t_meth  il2_class_get_method_from_name;
static t_field il2_class_get_field_from_name;
static t_sget  il2_field_static_get_value;

static void* m_FindClass(const char* cls) {
    size_t n = 0; void** a = il2_domain_get_assemblies(il2_domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        void* k = il2_class_from_name(il2_assembly_get_image(a[i]), "", cls);
        if (k) return k;
    }
    return nullptr;
}
static void* m_FindMethodNS(const char* ns, const char* cls, const char* m, int argc) {
    size_t n = 0; void** a = il2_domain_get_assemblies(il2_domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        void* k = il2_class_from_name(il2_assembly_get_image(a[i]), ns, cls);
        if (!k) continue;
        void** mi = (void**) il2_class_get_method_from_name(k, m, argc);
        if (mi) return *mi;
    }
    LOGE("method not found: %s::%s", cls, m);
    return nullptr;
}
static void* m_FindMethod(const char* cls, const char* m, int argc) { return m_FindMethodNS("", cls, m, argc); }

static void* m_GetStatic(const char* cls, const char* field) {
    void* k = m_FindClass(cls); if (!k) return nullptr;
    void* f = il2_class_get_field_from_name(k, field); if (!f) return nullptr;
    void* out = nullptr; il2_field_static_get_value(f, &out); return out;
}

// ---------- অবস্থা ----------
static bool oneHitKill = false, unlimitedHealth = false, unlimitedGloo = false, unlimitedAmmo = false;
static void *g_pc = nullptr, *g_ph = nullptr;   // নিজের PlayerController / PlayerHealth (PlayerController.Update এর self)
static int  g_frame = 0;
static bool pcHookOk = false, mcHookOk = false;
static unsigned long pcTicks = 0, mcTicks = 0;
static int  gunSlot = 0, gunNumber = 0;

static void RunQueue();
struct V3 { float x, y, z; };
struct Q4 { float x, y, z, w; };

static void* LocalPlayerHealth() { return g_ph; }

// ---------- হুক ----------
// Unlimited Health
static void (*old_GetDamage)(void*, int, uint8_t, void*);
static void new_GetDamage(void* self, int d, uint8_t actor, void* mi) {
    if (unlimitedHealth && self == LocalPlayerHealth()) return;
    old_GetDamage(self, d, actor, mi);
}
static void (*old_ApplyDamagePlayer)(void*, int, uint8_t, V3, Q4, void*);
static void new_ApplyDamagePlayer(void* self, int d, uint8_t actor, V3 p, Q4 r, void* mi) {
    if (unlimitedHealth && self == LocalPlayerHealth()) return;
    old_ApplyDamagePlayer(self, d, actor, p, r, mi);
}

// One Hit Kill (শুধু প্লেয়ারের আঘাতে)
static void (*old_AI_TakeDamage)(void*, int16_t, uint8_t, bool, void*);
static void new_AI_TakeDamage(void* self, int16_t a, uint8_t actor, bool isPlayer, void* mi) {
    old_AI_TakeDamage(self, (oneHitKill && isPlayer) ? (int16_t) 30000 : a, actor, isPlayer, mi);
}
static void (*old_AI_TakeDamagePV)(void*, int16_t, uint8_t, bool, void*);
static void new_AI_TakeDamagePV(void* self, int16_t a, uint8_t actor, bool isPlayer, void* mi) {
    old_AI_TakeDamagePV(self, (oneHitKill && isPlayer) ? (int16_t) 30000 : a, actor, isPlayer, mi);
}
static void (*old_AI_ApplyDamage)(void*, int, uint8_t, bool, void*);
static void new_AI_ApplyDamage(void* self, int a, uint8_t actor, bool isPlayer, void* mi) {
    old_AI_ApplyDamage(self, (oneHitKill && isPlayer) ? 30000 : a, actor, isPlayer, mi);
}

// Unlimited Gloo Wall
static void (*AddGlooWalls)(void*, int, void*);
static void* (*FindObjectsOfType)(void*, void*);              // UnityEngine.Object.FindObjectsOfType(Type)
static void* aiType = nullptr;
static void (*old_PC_Update)(void*, void*);
static void new_PC_Update(void* self, void* mi) {
    old_PC_Update(self, mi);
    g_pc = self;
    pcTicks++;
    g_ph = *(void**)((char*)self + 0x60);                      // PlayerController.playerHealth
    RunQueue();
    g_frame++;
    if (g_ph && unlimitedHealth) {                             // currentHealth = maxHealth
        *(int*)((char*)g_ph + 0x3C) = *(int*)((char*)g_ph + 0x38);
    }
    if (unlimitedAmmo) {                                       // Weapon.currentAmmo / totalAmmo
        void* w = *(void**)((char*)self + 0x208);              // currentEquipedWeapon
        if (w) {
            int mag = *(int*)((char*)w + 0x68);
            if (mag > 0) *(int*)((char*)w + 0x70) = mag;
            *(int*)((char*)w + 0x6C) = 999;
        }
    }
    if (unlimitedGloo && AddGlooWalls) {
        int cur = *(int*)((char*)self + 0x2CC);                // WallCount
        if (cur < 20) AddGlooWalls(self, 20 - cur, nullptr);
    }
    if (oneHitKill && (g_frame % 12 == 0) && FindObjectsOfType) {   // শত্রু বটের হেলথ = 1
        if (!aiType) { void* k = m_FindClass("AIHealth"); if (k) aiType = il2_type_get_object(il2_class_get_type(k)); }
        if (aiType) {
            void* arr = FindObjectsOfType(aiType, nullptr);
            if (arr) {
                size_t n = *(size_t*)((char*)arr + 0x18); void** el = (void**)((char*)arr + 0x20);
                for (size_t i = 0; i < n && i < 128; i++)
                    if (el[i]) *(int16_t*)((char*)el[i] + 0x22) = 1;     // AIHealth.currentHealth
            }
        }
    }
}

static void (*old_MC_Update)(void*, void*);
static void new_MC_Update(void* self, void* mi) { old_MC_Update(self, mi); mcTicks++; RunQueue(); }

// ---------- দরকারি মেথড ----------
static void (*CoinTransation)(void*, int, int, void*);
static void (*DimondTransation)(void*, int, int, void*);
static void (*GM_AddMoney)(void*, int, void*);
static void (*AddMedkitsCS)(void*, int, void*);
static void (*PC_GetItem)(void*, void*, void*);              // PlayerController.GetItem(WeaponData)
static void (*InvSetGun)(void*, int, void*, void*);          // Inventory.SetGun(int type, WeaponData)
static void* (*FindObjectsOfTypeAll)(void*, void*);          // UnityEngine.Resources

static std::vector<void*> weapons;
static std::string m_Str(void* s) {
    if (!s) return "";
    int len = *(int*)((char*)s + 0x10); uint16_t* c = (uint16_t*)((char*)s + 0x14);
    std::string r; for (int i = 0; i < len; i++) r += (char) c[i]; return r;
}
static void RefreshWeapons() {
    weapons.clear();
    // ১) নিজের PlayerController.AllWeapons (Weapon[]) থেকে .data (WeaponData)
    if (g_pc) {
        void* arr = *(void**)((char*)g_pc + 0x218);
        if (arr) {
            size_t n = *(size_t*)((char*)arr + 0x18); void** el = (void**)((char*)arr + 0x20);
            for (size_t i = 0; i < n && i < 200; i++) {
                if (!el[i]) continue;
                void* d = *(void**)((char*)el[i] + 0x20);     // Weapon.data
                if (!d) continue;
                bool dup = false; for (void* x : weapons) if (x == d) dup = true;
                if (!dup) weapons.push_back(d);
            }
        }
    }
    // ২) না পেলে: Resources থেকে সব WeaponData
    if (weapons.empty() && FindObjectsOfTypeAll) {
        void* k = m_FindClass("WeaponData");
        if (k) {
            void* t = il2_type_get_object(il2_class_get_type(k));
            void* arr = FindObjectsOfTypeAll(t, nullptr);
            if (arr) {
                size_t n = *(size_t*)((char*)arr + 0x18); void** el = (void**)((char*)arr + 0x20);
                for (size_t i = 0; i < n && i < 400; i++) if (el[i]) weapons.push_back(el[i]);
            }
        }
    }
}
static void PrintWeapons() {
    RefreshWeapons();
    for (size_t i = 0; i < weapons.size(); i++)
        LOGI("FFIMOD gun #%zu = %s", i, m_Str(*(void**)((char*)weapons[i] + 0x28)).c_str());
}

// ---------- মূল (Unity) থ্রেডে কাজ চালানোর লাইন ----------
// মেনুর বাটন Java থ্রেডে চাপা হয়। সেখান থেকে সরাসরি গেমের ফাংশন কল করা অনিরাপদ (ক্র্যাশের কারণ)।
// তাই কাজগুলো লাইনে রাখা হয় এবং গেমের নিজের Update এর ভেতর থেকে চালানো হয়।
static std::mutex qMtx;
static std::vector<std::function<void()>> qTasks;
static std::mutex gnMtx;
static std::string gunNamesCache;

static void PostTask(std::function<void()> f) {
    std::lock_guard<std::mutex> l(qMtx);
    if (qTasks.size() < 64) qTasks.push_back(f);
}
static void RunQueue() {
    std::vector<std::function<void()>> local;
    { std::lock_guard<std::mutex> l(qMtx); if (qTasks.empty()) return; local.swap(qTasks); }
    for (auto& f : local) f();
}
static void BuildGunNames() {
    RefreshWeapons();
    std::string r;
    for (size_t i = 0; i < weapons.size(); i++) {
        std::string n = m_Str(*(void**)((char*)weapons[i] + 0x28));
        if (n.empty()) n = "Weapon";
        r += n; if (i + 1 < weapons.size()) r += "\n";
    }
    std::lock_guard<std::mutex> l(gnMtx);
    gunNamesCache = r;
}

// ড্যামেজ হুক: শুধু প্রথমবার টগল অন করলে বসে (মূল থ্রেডে)। শুধু ১৬ বাইটের বেশি বড় মেথড হুক করা হয়।
// AIHealth.TakeDamage ও ApplyDamage মাত্র ৮ বাইট (পাশের মেথড নষ্ট করত) তাই হুক করা হয় না।
static bool dmgHooked = false;
static void HookIt(void* addr, void* nw, void** old);
static void InstallDamageHooks() {
    if (dmgHooked) return;
    dmgHooked = true;
    HookIt(m_FindMethod("PlayerHealth", "GetDamage", 2), (void*) new_GetDamage, (void**) &old_GetDamage);
    HookIt(m_FindMethod("PlayerHealth", "ApplyDamagePlayer", 4), (void*) new_ApplyDamagePlayer, (void**) &old_ApplyDamagePlayer);
    HookIt(m_FindMethod("AIHealth", "TakeDamagePV", 3), (void*) new_AI_TakeDamagePV, (void**) &old_AI_TakeDamagePV);
}

// সব কাজ এক জায়গায় (নতুন মেনু ও পুরোনো মেনু দুটোই ব্যবহার করে)
static void DoAction(int id, int value, bool on) {
    switch (id) {
        case 1: oneHitKill = on; if (on) PostTask([]() { InstallDamageHooks(); }); break;
        case 2: unlimitedHealth = on; if (on) PostTask([]() { InstallDamageHooks(); }); break;
        case 3: unlimitedGloo = on; break;
        case 6: unlimitedAmmo = on; break;
        case 4: PostTask([value]() { if (g_pc && AddGlooWalls) AddGlooWalls(g_pc, value, nullptr); }); break;
        case 5: PostTask([value]() { if (g_ph && AddMedkitsCS) AddMedkitsCS(g_ph, value, nullptr); }); break;
        case 8: PostTask([value]() { int idx = value % 1000;               // পদ্ধতি A: তুলে নেওয়া (GetItem)
                    if (weapons.empty()) RefreshWeapons();
                    if (g_pc && PC_GetItem && idx >= 0 && idx < (int) weapons.size()) PC_GetItem(g_pc, weapons[idx], nullptr); }); break;
        case 13: PostTask([value]() { int slot = value / 1000, idx = value % 1000;   // পদ্ধতি B: স্লটে বসানো (SetGun)
                    void* inv = m_GetStatic("Inventory", "Singleton");
                    if (weapons.empty()) RefreshWeapons();
                    if (inv && InvSetGun && idx >= 0 && idx < (int) weapons.size()) InvSetGun(inv, slot, weapons[idx], nullptr); }); break;
        case 9: PostTask([]() { BuildGunNames(); }); break;
        case 10: PostTask([value]() { void* m = m_GetStatic("MenuController", "instance");
                    if (m && CoinTransation) CoinTransation(m, value >= 0 ? 0 : 1, abs(value), nullptr); }); break;
        case 11: PostTask([value]() { void* m = m_GetStatic("MenuController", "instance");
                    if (m && DimondTransation) DimondTransation(m, value >= 0 ? 0 : 1, abs(value), nullptr); }); break;
        case 12: PostTask([value]() { void* g = m_GetStatic("GameManager", "instace"); if (!g) g = m_GetStatic("GameManager", "instance");
                    if (g && GM_AddMoney) GM_AddMoney(g, value, nullptr); }); break;
        default: break;
    }
}

// নতুন FfiMenu (Java) এর native ফাংশন
static std::string StatusText() {
    char b[256];
    snprintf(b, sizeof b, "PlayerHook:%s ticks=%lu | MenuHook:%s ticks=%lu | player:%s | guns:%d | gloo:%s medkit:%s gun:%s",
             pcHookOk ? "OK" : "NOT FOUND", pcTicks, mcHookOk ? "OK" : "NOT FOUND", mcTicks,
             g_ph ? "found" : "none", (int) weapons.size(),
             AddGlooWalls ? "ok" : "no", AddMedkitsCS ? "ok" : "no", PC_GetItem ? "ok" : "no");
    return std::string(b);
}
extern "C" {
JNIEXPORT jstring JNICALL Java_com_ffimod_FfiMenu_nStatus(JNIEnv* env, jclass) { return env->NewStringUTF(StatusText().c_str()); }
JNIEXPORT void JNICALL Java_com_ffimod_FfiMenu_nToggle(JNIEnv*, jclass, jint id, jboolean on) { DoAction(id, 0, on); }
JNIEXPORT void JNICALL Java_com_ffimod_FfiMenu_nAction(JNIEnv*, jclass, jint id, jint value) { DoAction(id, value, true); }
JNIEXPORT jstring JNICALL Java_com_ffimod_FfiMenu_nGunNames(JNIEnv* env, jclass) {
    std::string r; { std::lock_guard<std::mutex> l(gnMtx); r = gunNamesCache; }
    return env->NewStringUTF(r.c_str());
}
}

// ---------- মেনু তালিকা ----------
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
    jobjectArray ret;

    const char *features[] = {
            OBFUSCATE("Category_PLAYER"),
            OBFUSCATE("1_Toggle_One Hit Kill"),
            OBFUSCATE("2_Toggle_Unlimited Health"),

            OBFUSCATE("Category_ITEMS"),
            OBFUSCATE("3_Toggle_Unlimited Global (Gloo Wall)"),
            OBFUSCATE("4_InputValue_Add Global"),
            OBFUSCATE("5_InputValue_Add Medkit"),

            OBFUSCATE("Category_WEAPONS"),
            OBFUSCATE("6_Spinner_Gun Slot_Slot 1,Slot 2,Slot 3"),
            OBFUSCATE("7_InputValue_Gun Number"),
            OBFUSCATE("8_Button_Add Gun"),
            OBFUSCATE("9_Button_Print Gun List (logcat)"),

            OBFUSCATE("Category_CURRENCY"),
            OBFUSCATE("10_InputValue_Add/Remove Money (Lobby)"),
            OBFUSCATE("11_InputValue_Add/Remove Diamond (Lobby)"),
            OBFUSCATE("12_InputValue_Add Money (In Match)"),
    };

    int Total_Feature = (sizeof features / sizeof features[0]);
    ret = (jobjectArray)
            env->NewObjectArray(Total_Feature, env->FindClass(OBFUSCATE("java/lang/String")),
                                env->NewStringUTF(""));
    for (int i = 0; i < Total_Feature; i++)
        env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    return (ret);
}

// ---------- মেনুর বাটন/টগল ----------
void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName, jint value, jlong Lvalue, jboolean boolean, jstring text) {
    DoAction(featNum, value, boolean);
}

// ---------- হুক বসানো ----------
static void HookIt(void* addr, void* nw, void** old) {
    if (addr) DobbyHook(addr, (dobby_dummy_func_t) nw, (dobby_dummy_func_t *) old);
}

void hack_thread() {
    while (!isLibraryLoaded(targetLibName)) sleep(1);   // libil2cpp লোড হওয়া পর্যন্ত অপেক্ষা
#if defined(__aarch64__)
    void* h = dlopen("libil2cpp.so", RTLD_NOLOAD);
    if (!h) { LOGE("libil2cpp handle null"); return; }
    il2_domain_get = (t_void) dlsym(h, "il2cpp_domain_get");
    il2_domain_get_assemblies = (t_asms) dlsym(h, "il2cpp_domain_get_assemblies");
    il2_assembly_get_image = (t_p1) dlsym(h, "il2cpp_assembly_get_image");
    il2_thread_attach = (t_p1) dlsym(h, "il2cpp_thread_attach");
    il2_thread_detach = (t_p1) dlsym(h, "il2cpp_thread_detach");
    il2_class_from_name = (t_cls) dlsym(h, "il2cpp_class_from_name");
    il2_class_get_method_from_name = (t_meth) dlsym(h, "il2cpp_class_get_method_from_name");
    il2_class_get_field_from_name = (t_field) dlsym(h, "il2cpp_class_get_field_from_name");
    il2_field_static_get_value = (t_sget) dlsym(h, "il2cpp_field_static_get_value");
    il2_class_get_type = (t_p1) dlsym(h, "il2cpp_class_get_type");
    il2_type_get_object = (t_p1) dlsym(h, "il2cpp_type_get_object");
    if (!il2_domain_get || !il2_class_from_name) { LOGE("il2cpp api missing"); return; }

    sleep(10);                                           // গেম পুরো লোড হতে দিন
    void* myThread = il2_thread_attach(il2_domain_get());

    void* mcUpd = m_FindMethod("MenuController", "Update", 0);
    mcHookOk = (mcUpd != nullptr);
    HookIt(mcUpd, (void*) new_MC_Update, (void**) &old_MC_Update);
    void* pcUpd = m_FindMethodNS("WeirdBrothers", "PlayerController", "Update", 0);
    pcHookOk = (pcUpd != nullptr);
    HookIt(pcUpd, (void*) new_PC_Update, (void**) &old_PC_Update);

    AddGlooWalls     = (decltype(AddGlooWalls))     m_FindMethodNS("WeirdBrothers", "PlayerController", "AddGlooWalls", 1);
    CoinTransation   = (decltype(CoinTransation))   m_FindMethod("MenuController", "CoinTransation", 2);
    DimondTransation = (decltype(DimondTransation)) m_FindMethod("MenuController", "DimondTransation", 2);
    GM_AddMoney      = (decltype(GM_AddMoney))      m_FindMethod("GameManager", "AddMoney", 1);
    AddMedkitsCS     = (decltype(AddMedkitsCS))     m_FindMethod("PlayerHealth", "AddMedkitsCS", 1);
    InvSetGun        = (decltype(InvSetGun))        m_FindMethod("Inventory", "SetGun", 2);
    FindObjectsOfTypeAll = (decltype(FindObjectsOfTypeAll)) m_FindMethodNS("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1);
    FindObjectsOfType    = (decltype(FindObjectsOfType))    m_FindMethodNS("UnityEngine", "Object", "FindObjectsOfType", 1);
    PC_GetItem           = (decltype(PC_GetItem))           m_FindMethodNS("WeirdBrothers", "PlayerController", "GetItem", 1);
    if (il2_thread_detach && myThread) il2_thread_detach(myThread);   // থ্রেড শেষ হওয়ার আগে ডিটাচ (ক্র্যাশ এড়াতে)
#endif
    LOGI(OBFUSCATE("FFIMOD hooks ready"));
}

__attribute__((constructor))
void lib_main() {
    std::thread(hack_thread).detach();
}
