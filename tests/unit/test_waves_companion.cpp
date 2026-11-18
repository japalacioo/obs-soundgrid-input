// Reapertura del SoundGrid Driver Control Panel y del servicio de Waves (lógica sin E/S).
#include "platform/WavesCompanion.h"

#include <gtest/gtest.h>

#include <set>

using namespace sg;

namespace {
constexpr int64_t kMs = 1'000'000;

struct FakeOps : IProcessOps {
    std::set<std::string> running;
    int launches = 0;
    bool startsOnLaunch = true; // el proceso aparece al abrirlo
    bool failLaunch = false;
    bool isRunning(const std::string &n) override { return running.count(n) > 0; }
    bool launch(const LaunchTarget &t, std::string &error) override
    {
        ++launches;
        if (failLaunch) {
            error = "requiere permisos de administrador (no se pide UAC)";
            return false;
        }
        if (startsOnLaunch)
            running.insert(t.processName);
        return true;
    }
};

LaunchTarget panel()
{
    return {"SoundGrid Driver Control Panel", "panel.exe", "C:\\panel.exe", ""};
}
} // namespace

TEST(WavesKeepAlive, OpensItWhenNotRunning)
{
    FakeOps ops;
    KeepAlive k(panel());
    const auto msg = k.poll(ops, true, 0);
    ASSERT_TRUE(msg);
    EXPECT_NE(msg->find("se abrió"), std::string::npos);
    EXPECT_EQ(ops.launches, 1);
    EXPECT_EQ(k.status().relaunches, 1);
}

TEST(WavesKeepAlive, DoesNothingWhenDisabledOrAlreadyRunning)
{
    FakeOps ops;
    KeepAlive k(panel());
    EXPECT_FALSE(k.poll(ops, false, 0));
    EXPECT_EQ(ops.launches, 0);
    ops.running.insert("panel.exe");
    EXPECT_FALSE(k.poll(ops, true, 2000 * kMs));
    EXPECT_EQ(ops.launches, 0);
    EXPECT_TRUE(k.status().running);
}

TEST(WavesKeepAlive, ReopensWhenClosedDuringStreaming)
{
    FakeOps ops;
    ops.running.insert("panel.exe");
    KeepAlive k(panel());
    k.poll(ops, true, 0);
    ops.running.clear(); // el operador lo cierra a mitad de la transmisión
    const auto msg = k.poll(ops, true, 600'000 * kMs);
    ASSERT_TRUE(msg);
    EXPECT_NE(msg->find("se cerró"), std::string::npos);
    EXPECT_NE(msg->find("se abrió"), std::string::npos);
    EXPECT_EQ(ops.launches, 1);
}

TEST(WavesKeepAlive, WaitsWhileTheLaunchedProcessIsStarting)
{
    FakeOps ops;
    ops.startsOnLaunch = false; // tarda en aparecer
    KeepAlive k(panel());
    k.poll(ops, true, 0);
    for (int64_t t = 2000; t < 15000; t += 2000)
        k.poll(ops, true, t * kMs);
    EXPECT_EQ(ops.launches, 1); // no lo abre dos veces mientras arranca
}

TEST(WavesKeepAlive, GivesUpAfterRepeatedQuickExits)
{
    FakeOps ops;
    KeepAlive k(panel());
    int64_t t = 0;
    for (int i = 0; i < 10; ++i) {
        k.poll(ops, true, t * kMs); // lo abre
        t += 2000;
        k.poll(ops, true, t * kMs); // lo ve abierto
        ops.running.clear();        // y se cierra enseguida
        t += 2000;
    }
    k.poll(ops, true, t * kMs);
    EXPECT_TRUE(k.status().gaveUp);
    EXPECT_LE(ops.launches, 5);
    const int before = ops.launches;
    k.poll(ops, true, (t + 60000) * kMs);
    EXPECT_EQ(ops.launches, before); // rendido: no insiste
    k.resetGiveUp();                 // botón "Abrir" del panel
    k.poll(ops, true, (t + 62000) * kMs);
    EXPECT_EQ(ops.launches, before + 1);
}

TEST(WavesKeepAlive, ReportsLaunchErrorsWithoutLooping)
{
    FakeOps ops;
    ops.failLaunch = true;
    KeepAlive k(panel());
    const auto msg = k.poll(ops, true, 0);
    ASSERT_TRUE(msg);
    EXPECT_NE(msg->find("administrador"), std::string::npos);
    for (int64_t t = 2000; t < 600000; t += 2000)
        k.poll(ops, true, t * kMs);
    EXPECT_TRUE(k.status().gaveUp);
    EXPECT_LE(ops.launches, 5);
}

TEST(WavesKeepAlive, StableRunForgetsPreviousFailures)
{
    FakeOps ops;
    KeepAlive k(panel());
    k.poll(ops, true, 0);
    k.poll(ops, true, 2000 * kMs);
    ops.running.clear(); // un cierre rápido
    k.poll(ops, true, 4000 * kMs);
    k.poll(ops, true, 6000 * kMs);           // reabierto y visto
    k.poll(ops, true, (6000 + 130000) * kMs); // estable más de 2 min
    for (int i = 0; i < 4; ++i) {             // 4 cierres rápidos más: aún no se rinde
        ops.running.clear();
        k.poll(ops, true, (140000 + i * 4000) * kMs);
        k.poll(ops, true, (142000 + i * 4000) * kMs);
    }
    EXPECT_FALSE(k.status().gaveUp);
}
