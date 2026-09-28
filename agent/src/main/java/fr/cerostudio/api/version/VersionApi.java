package fr.cerostudio.api.version;

public final class VersionApi {

    private static final String FALLBACK_AGENT_VERSION = "dev";

    private final String minecraftVersion;

    public VersionApi(String minecraftVersion) {
        this.minecraftVersion = minecraftVersion == null ? "inconnue" : minecraftVersion;
    }

    public String agent() {
        String version = null;
        Package agentPackage = VersionApi.class.getPackage();
        if (agentPackage != null) {
            version = agentPackage.getImplementationVersion();
        }
        return version != null && !version.isEmpty() ? version : FALLBACK_AGENT_VERSION;
    }

    public String minecraft() {
        return minecraftVersion;
    }
}