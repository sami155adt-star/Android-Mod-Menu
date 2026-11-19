// FFI offline game - LGL Mod Menu (Android-Mod-Menu) এর jni/Main.cpp
// dump.cs থেকে পাওয়া আসল ক্লাস/মেথডের নাম বসানো আছে। অফসেট নয়, নাম দিয়ে ফাংশন খোঁজা হয়।
// অনুমান/ঝুঁকির জায়গাগুলোয় "CHECK:" লেখা আছে।
//
// গেমের "গ্লোবাল" = Gloo Wall (PlayerController.WallCount, AddGlooWalls)
// লবির Coin/Diamond = MenuController.CoinTransation / DimondTransation (শুধু লবিতে কাজ করবে)
// ম্যাচের ভেতরের টাকা = GameManager.AddMoney (CS মোডের টাকা)

#include <jni.h>
#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <cstdlib>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"
#include "Menu.h"
#include "dobby.h"

#define TAG "FFIMOD"

// ---------- IL2CPP API ----------
typedef void*  (*t_void)();
typedef void** (*t_asms)(void*, size_t*);
typedef void*  (*t_p1)(void*);
typedef void*  (*t_cls)(void*, const char*, const char*);
typedef void*  (*t_meth)(void*, const char*, int);
typedef void*  (*t_field)(void*, const char*);
typedef void   (*t_sget)(void*, void*);
static t_void il2cpp_domain_get;      static t_asms il2cpp_domain_get_assemblies;
static t_p1   il2cpp_assembly_get_image, il2cpp_thread_attach, il2cpp_class_get_type, il2cpp_type_get_object;
static t_cls  il2cpp_class_from_name; static t_meth il2cpp_class_get_method_from_name;
static t_field il2cpp_class_get_field_from_name; static t_sget il2cpp_field_static_get_value;

static void* FindClass(const char* cls) {
    size_t n = 0; void** a = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        void* k = il2cpp_class_from_name(il2cpp_assembly_get_image(a[i]), "", cls);
        if (k) return k;
    }
    return nullptr;
}
static void* FindMethodNS(const char* ns, const char* cls, const char* m, int argc) {
    size_t n = 0; void** a = il2cpp_domain_get_assemblies(il2cpp_domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        void* k = il2cpp_class_from_name(il2cpp_assembly_get_image(a[i]), ns, cls);
        if (!k) continue;
        void** mi = (void**) il2cpp_class_get_method_from_name(k, m, argc);
        if (mi) return *mi;
    }
    LOGE("method not found: %s::%s", cls, m);
    return nullptr;
}
static void* FindMethod(const char* cls, const char* m, int argc) { return FindMethodNS("", cls, m, argc); }

// static ফিল্ড পড়া (যেমন MenuController.instance)
static void* GetStatic(const char* cls, const char* field) {
    void* k = FindClass(cls); if (!k) return nullptr;
    void* f = il2cpp_class_get_field_from_name(k, field); if (!f) return nullptr;
    void* out = nullptr; il2cpp_field_static_get_value(f, &out); return out;
}

// ---------- অবস্থা ----------
static bool oneHitKill, unlimitedHealth, unlimitedGloo;
static int  gunSlot = 0, gunNumber = 0;

struct V3 { float x, y, z; };
struct Q4 { float x, y, z, w; };

static void* LocalPlayerHealth() { return GetStatic("GameManager", "NewPlayer"); } // CHECK: NewPlayer = নিজের খেলোয়াড় কিনা

// ---------- হুক ----------
// Unlimited Health (শুধু নিজের PlayerHealth এ)
void (*old_GetDamage)(void*, int, uint8_t, void*);
void new_GetDamage(void* self, int d, uint8_t actor, void* mi) {
    if (unlimitedHealth && self == LocalPlayerHealth()) return;
    old_GetDamage(self, d, actor, mi);
}
void (*old_ApplyDamagePlayer)(void*, int, uint8_t, V3, Q4, void*);
void new_ApplyDamagePlayer(void* self, int d, uint8_t actor, V3 p, Q4 r, void* mi) {
    if (unlimitedHealth && self == LocalPlayerHealth()) return;
    old_ApplyDamagePlayer(self, d, actor, p, r, mi);
}

// One Hit Kill: শুধু যখন আঘাতকারী প্লেয়ার (isPlayer=true), বটের ক্ষতি বিশাল করা
void (*old_AI_TakeDamage)(void*, int16_t, uint8_t, bool, void*);
void new_AI_TakeDamage(void* self, int16_t a, uint8_t actor, bool isPlayer, void* mi) {
    old_AI_TakeDamage(self, (oneHitKill && isPlayer) ? (int16_t) 30000 : a, actor, isPlayer, mi);
}
void (*old_AI_TakeDamagePV)(void*, int16_t, uint8_t, bool, void*);
void new_AI_TakeDamagePV(void* self, int16_t a, uint8_t actor, bool isPlayer, void* mi) {
    old_AI_TakeDamagePV(self, (oneHitKill && isPlayer) ? (int16_t) 30000 : a, actor, isPlayer, mi);
}
void (*old_AI_ApplyDamage)(void*, int, uint8_t, bool, void*);
void new_AI_ApplyDamage(void* self, int a, uint8_t actor, bool isPlayer, void* mi) {
    old_AI_ApplyDamage(self, (oneHitKill && isPlayer) ? 30000 : a, actor, isPlayer, mi);
}

// Unlimited Gloo: PlayerController.Update এ প্রতি ফ্রেমে চেক
static void (*AddGlooWalls)(void*, int, void*);
void (*old_PC_Update)(void*, void*);
void new_PC_Update(void* self, void* mi) {
    old_PC_Update(self, mi);
    if (unlimitedGloo && AddGlooWalls) {
        int cur = *(int*)((char*)self + 0x2CC);           // WallCount
        if (cur < 20) AddGlooWalls(self, 20 - cur, nullptr); // 0 থাকলেও সাথে সাথে বাটন দেখাবে
    }
}

// ---------- দরকারি মেথড ----------
static void (*CoinTransation)(void*, int, int, void*);
static void (*DimondTransation)(void*, int, int, void*);
static void (*GM_AddMoney)(void*, int, void*);
static void (*AddMedkitsCS)(void*, int, void*);
static void (*SetGun)(void*, int, void*, void*);          // Inventory.SetGun(int type, WeaponData)
static void* (*FindObjectsOfTypeAll)(void*, void*);       // UnityEngine.Resources

// WeaponData তালিকা
static std::vector<void*> weapons;
static std::string Str(void* s) {                         // Il2CppString -> std::string
    if (!s) return "";
    int len = *(int*)((char*)s + 0x10); uint16_t* c = (uint16_t*)((char*)s + 0x14);
    std::string r; for (int i = 0; i < len; i++) r += (char) c[i]; return r;
}
static void RefreshWeapons() {
    weapons.clear();
    void* k = FindClass("WeaponData"); if (!k || !FindObjectsOfTypeAll) return;
    void* t = il2cpp_type_get_object(il2cpp_class_get_type(k));
    void* arr = FindObjectsOfTypeAll(t, nullptr);
    if (!arr) return;
    size_t n = *(size_t*)((char*)arr + 0x18); void** el = (void**)((char*)arr + 0x20);
    for (size_t i = 0; i < n; i++) weapons.push_back(el[i]);
}
static void PrintWeapons() {
    RefreshWeapons();
    for (size_t i = 0; i < weapons.size(); i++)
        LOGI("[%s] gun #%zu = %s", TAG, i, Str(*(void**)((char*)weapons[i] + 0x28)).c_str()); // WeaponName @0x28
}

static void Hooks() {
    void* h; while (!(h = dlopen("libil2cpp.so", RTLD_NOLOAD))) sleep(1);
    #define SYM(n) n = (decltype(n)) dlsym(h, #n)
    SYM(il2cpp_domain_get); SYM(il2cpp_domain_get_assemblies); SYM(il2cpp_assembly_get_image);
    SYM(il2cpp_thread_attach); SYM(il2cpp_class_from_name); SYM(il2cpp_class_get_method_from_name);
    SYM(il2cpp_class_get_field_from_name); SYM(il2cpp_field_static_get_value);
    SYM(il2cpp_class_get_type); SYM(il2cpp_type_get_object);
    sleep(10);
    il2cpp_thread_attach(il2cpp_domain_get());

    void* a;
    #define HOOKM(cls, m, argc, nw, old) if ((a = FindMethod(cls, m, argc))) DobbyHook(a, (void*)nw, (void**)&old)
    HOOKM("PlayerHealth", "GetDamage", 2, new_GetDamage, old_GetDamage);
    HOOKM("PlayerHealth", "ApplyDamagePlayer", 4, new_ApplyDamagePlayer, old_ApplyDamagePlayer);
    HOOKM("AIHealth", "TakeDamage", 3, new_AI_TakeDamage, old_AI_TakeDamage);
    HOOKM("AIHealth", "TakeDamagePV", 3, new_AI_TakeDamagePV, old_AI_TakeDamagePV);
    HOOKM("AIHealth", "ApplyDamage", 3, new_AI_ApplyDamage, old_AI_ApplyDamage);
    HOOKM("PlayerController", "Update", 0, new_PC_Update, old_PC_Update);

    AddGlooWalls     = (decltype(AddGlooWalls))     FindMethod("PlayerController", "AddGlooWalls", 1);
    CoinTransation   = (decltype(CoinTransation))   FindMethod("MenuController", "CoinTransation", 2);
    DimondTransation = (decltype(DimondTransation)) FindMethod("MenuController", "DimondTransation", 2);
    GM_AddMoney      = (decltype(GM_AddMoney))      FindMethod("GameManager", "AddMoney", 1);
    AddMedkitsCS     = (decltype(AddMedkitsCS))     FindMethod("PlayerHealth", "AddMedkitsCS", 1);
    SetGun           = (decltype(SetGun))           FindMethod("Inventory", "SetGun", 2);
    FindObjectsOfTypeAll = (decltype(FindObjectsOfTypeAll)) FindMethodNS("UnityEngine", "Resources", "FindObjectsOfTypeAll", 1);
    LOGI("[%s] hooks ready", TAG);
}
void* hack_thread(void*) { Hooks(); return nullptr; }

// ---------- মেনু ----------
jobjectArray GetFeatureList(JNIEnv *env, jobject context) {
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
    int n = sizeof(features) / sizeof(features[0]);
    jobjectArray ret = env->NewObjectArray(n, env->FindClass(OBFUSCATE("java/lang/String")), env->NewStringUTF(""));
    for (int i = 0; i < n; i++) env->SetObjectArrayElement(ret, i, env->NewStringUTF(features[i]));
    return ret;
}

void Changes(JNIEnv *env, jclass clazz, jobject obj, jint featNum, jstring featName, jint value,
             jboolean boolean, jstring str) {
    switch (featNum) {
        case 1: oneHitKill = boolean; break;
        case 2: unlimitedHealth = boolean; break;
        case 3: unlimitedGloo = boolean; break;
        case 4: { void* p = LocalPlayerHealth(); if (p && AddGlooWalls) {
                    void* ctrl = *(void**)((char*)p + 0x30);     // PlayerHealth.Controller
                    if (ctrl) AddGlooWalls(ctrl, value, nullptr); } break; }
        case 5: { void* p = LocalPlayerHealth(); if (p && AddMedkitsCS) AddMedkitsCS(p, value, nullptr); break; }
        case 6: gunSlot = value; break;
        case 7: gunNumber = value; break;
        case 8: { void* inv = GetStatic("Inventory", "Singleton");
                  RefreshWeapons();
                  if (inv && SetGun && gunNumber >= 0 && gunNumber < (int) weapons.size())
                      SetGun(inv, gunSlot, weapons[gunNumber], nullptr);
                  break; }
        case 9: PrintWeapons(); break;
        case 10: { void* m = GetStatic("MenuController", "instance");
                   if (m && CoinTransation) CoinTransation(m, value >= 0 ? 0 : 1, abs(value), nullptr); break; }
        case 11: { void* m = GetStatic("MenuController", "instance");
                   if (m && DimondTransation) DimondTransation(m, value >= 0 ? 0 : 1, abs(value), nullptr); break; }
        case 12: { void* g = GetStatic("GameManager", "instance");   // CHECK: static ফিল্ডের নাম (instance/Instance) যাচাই করুন
                   if (g && GM_AddMoney) GM_AddMoney(g, value, nullptr); break; }
    }
}

// ---------- JNI (LGL টেমপ্লেটের মতোই) ----------
__attribute__((constructor)) void lib_main() { pthread_t t; pthread_create(&t, nullptr, hack_thread, nullptr); }

int RegisterMenu(JNIEnv *env) {
    JNINativeMethod methods[] = {
        {OBFUSCATE("Icon"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(Icon)},
        {OBFUSCATE("IconWebViewData"), OBFUSCATE("()Ljava/lang/String;"), reinterpret_cast<void *>(IconWebViewData)},
        {OBFUSCATE("IsGameLibLoaded"), OBFUSCATE("()Z"), reinterpret_cast<void *>(isGameLibLoaded)},
        {OBFUSCATE("Init"), OBFUSCATE("(Landroid/content/Context;Landroid/widget/TextView;Landroid/widget/TextView;)V"), reinterpret_cast<void *>(Init)},
        {OBFUSCATE("SettingsList"), OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(SettingsList)},
        {OBFUSCATE("GetFeatureList"), OBFUSCATE("()[Ljava/lang/String;"), reinterpret_cast<void *>(GetFeatureList)},
    };
    jclass c = env->FindClass(OBFUSCATE("com/android/support/Menu"));
    if (!c) return JNI_ERR;
    return env->RegisterNatives(c, methods, sizeof(methods) / sizeof(methods[0])) != 0 ? JNI_ERR : JNI_OK;
}
int RegisterPreferences(JNIEnv *env) {
    JNINativeMethod methods[] = {
        {OBFUSCATE("Changes"), OBFUSCATE("(Landroid/content/Context;ILjava/lang/String;IZLjava/lang/String;)V"), reinterpret_cast<void *>(Changes)},
    };
    jclass c = env->FindClass(OBFUSCATE("com/android/support/Preferences"));
    if (!c) return JNI_ERR;
    return env->RegisterNatives(c, methods, sizeof(methods) / sizeof(methods[0])) != 0 ? JNI_ERR : JNI_OK;
}
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env; vm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (RegisterMenu(env) != JNI_OK) return JNI_ERR;
    if (RegisterPreferences(env) != JNI_OK) return JNI_ERR;
    return JNI_VERSION_1_6;
}
