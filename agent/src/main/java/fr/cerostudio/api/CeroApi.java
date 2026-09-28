package fr.cerostudio.api;

import fr.cerostudio.api.capability.CapabilitySet;
import fr.cerostudio.api.config.ConfigApi;
import fr.cerostudio.api.discord.DiscordApi;
import fr.cerostudio.api.event.EventBus;
import fr.cerostudio.api.http.HttpApi;
import fr.cerostudio.api.launch.LaunchInfo;
import fr.cerostudio.api.launcher.LauncherApi;
import fr.cerostudio.api.logger.LoggerApi;
import fr.cerostudio.api.mod.ModLoader;
import fr.cerostudio.api.player.PlayerIdentity;
import fr.cerostudio.api.runtime.RuntimeApi;
import fr.cerostudio.api.scheduler.Scheduler;
import fr.cerostudio.api.service.ServiceRegistry;
import fr.cerostudio.api.session.SessionApi;
import fr.cerostudio.api.util.UtilApi;
import fr.cerostudio.api.version.VersionApi;
import fr.cerostudio.api.window.WindowApi;

import java.util.Map;

public final class CeroApi {

    private static EventBus eventBus;
    private static ServiceRegistry serviceRegistry;
    private static ModLoader modLoader;
    private static CapabilitySet capabilitySet;
    private static PlayerIdentity playerIdentity;
    private static WindowApi windowApi;
    private static Scheduler scheduler;
    private static LauncherApi launcherApi;
    private static RuntimeApi runtimeApi;
    private static SessionApi sessionApi;
    private static UtilApi utilApi;
    private static DiscordApi discordApi;
    private static HttpApi httpApi;
    private static ConfigApi configApi;
    private static LoggerApi loggerApi;
    private static VersionApi versionApi;
    private static volatile LaunchInfo launchInfo;

    private CeroApi() {}

    public static void bootstrap(CapabilitySet capabilities, ModLoader loader, PlayerIdentity identity) {

        System.out.println("[CeroApi] bootstrap() par CL=" + CeroApi.class.getClassLoader());

        if (eventBus != null) {
            throw new IllegalStateException("CeroApi déjà initialisée");
        }
        capabilitySet = capabilities;
        eventBus = new EventBus();
        serviceRegistry = new ServiceRegistry();
        modLoader = loader;
        playerIdentity = identity;
        windowApi = new WindowApi();
        scheduler = new Scheduler(eventBus);
        launcherApi = new LauncherApi();
        runtimeApi = new RuntimeApi();
        sessionApi = new SessionApi(eventBus, System.currentTimeMillis());
        utilApi = new UtilApi();
        discordApi = new DiscordApi(launcherApi);
        httpApi = new HttpApi();
        configApi = new ConfigApi(eventBus);
        loggerApi = new LoggerApi();
        versionApi = new VersionApi(capabilities.mcVersion());
    }

    public static void captureLaunchArguments(String[] args) {
        if (launchInfo != null) {
            return;
        }
        launchInfo = LaunchInfo.capture(args);
    }

    public static String[] launchArguments() {
        return require(launchInfo, "LaunchInfo").raw();
    }

    public static Map<String, String> getArguments() {
        return require(launchInfo, "LaunchInfo").all();
    }

    public static String getArgument(String key) {
        return require(launchInfo, "LaunchInfo").get(key);
    }
    
    public static LaunchInfo launch() {
        return require(launchInfo, "LaunchInfo");
    }

    public static EventBus events() {
        return require(eventBus, "EventBus");
    }

    public static ServiceRegistry services() {
        return require(serviceRegistry, "ServiceRegistry");
    }

    public static ModLoader mods() {
        return require(modLoader, "ModLoader");
    }

    public static CapabilitySet capabilities() {
        return require(capabilitySet, "CapabilitySet");
    }

    public static PlayerIdentity player() {
        return require(playerIdentity, "PlayerIdentity");
    }

    public static WindowApi window() {
        return require(windowApi, "WindowApi");
    }

    public static Scheduler scheduler() {
        return require(scheduler, "Scheduler");
    }

    public static LauncherApi launcher() {
        return require(launcherApi, "LauncherApi");
    }

    public static RuntimeApi runtime() {
        return require(runtimeApi, "RuntimeApi");
    }

    public static SessionApi session() {
        return require(sessionApi, "SessionApi");
    }

    public static UtilApi util() {
        return require(utilApi, "UtilApi");
    }

    public static DiscordApi discord() {
        return require(discordApi, "DiscordApi");
    }

    public static HttpApi http() {
        return require(httpApi, "HttpApi");
    }

    public static ConfigApi config() {
        return require(configApi, "ConfigApi");
    }

    public static LoggerApi logger() {
        return require(loggerApi, "LoggerApi");
    }

    public static VersionApi version() {
        return require(versionApi, "VersionApi");
    }

    public static String minecraftVersion() {
        return capabilities().mcVersion();
    }

    private static <T> T require(T value, String name) {
        if (value == null) {
            System.out.println("[CeroApi] require(" + name + ") échoue, CL=" + CeroApi.class.getClassLoader());
            throw new IllegalStateException(
                    name + " demandé avant CeroApi.bootstrap() — appel trop tôt dans le cycle de vie ?");
        }
        return value;
    }
}