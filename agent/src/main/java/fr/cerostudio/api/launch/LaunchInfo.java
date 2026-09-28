package fr.cerostudio.api.launch;

import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

public final class LaunchInfo {

    private static final LaunchInfo EMPTY =
            new LaunchInfo(new String[0], Collections.emptyMap());

    private static final Set<String> KNOWN_FLAGS;

    static {
        Set<String> flags = new HashSet<>(Arrays.asList(
                "demo",
                "disableMultiplayer",
                "disableChat",
                "checkGlErrors",
                "fullscreen"
        ));
        KNOWN_FLAGS = Collections.unmodifiableSet(flags);
    }

    private final String[] rawArgs;
    private final Map<String, String> parsed;

    private LaunchInfo(String[] rawArgs, Map<String, String> parsed) {
        this.rawArgs = rawArgs;
        this.parsed = parsed;
    }

    public static LaunchInfo capture(String[] args) {
        if (args == null || args.length == 0) {
            return EMPTY;
        }

        String[] raw = args.clone();
        Map<String, String> parsed = new LinkedHashMap<>();

        for (int i = 0; i < raw.length; i++) {
            String token = raw[i];
            if (!token.startsWith("-") || token.length() <= 1) {
                continue;
            }

            String key = token.startsWith("--") ? token.substring(2) : token.substring(1);

            String value;
            int eq = key.indexOf('=');
            if (eq >= 0) {
                value = key.substring(eq + 1);
                key = key.substring(0, eq);
            } else if (!KNOWN_FLAGS.contains(key)
                    && i + 1 < raw.length
                    && !looksLikeOption(raw[i + 1])) {
                value = raw[i + 1];
                i++;
            } else {
                value = "true";
            }

            parsed.putIfAbsent(key, value);
        }

        return new LaunchInfo(raw, Collections.unmodifiableMap(parsed));
    }

    private static boolean looksLikeOption(String token) {
        return token.startsWith("-") && token.length() > 1;
    }

    public String[] raw() { return rawArgs.clone(); }

    public Map<String, String> all() { return parsed; }

    public String get(String key) { return parsed.get(key); }
    public String get(String key, String defaultValue) { return parsed.getOrDefault(key, defaultValue); }
    public boolean has(String key) { return parsed.containsKey(key); }
    public boolean isEmpty() { return rawArgs.length == 0; }

    @Override
    public String toString() { return "LaunchInfo" + parsed; }
}