package fr.cerostudio.core.launcher;

public final class LauncherArgs {

    private static final String SYSTEM_PROPERTY_KEY = "ceroclient.launcher.port";
    private static final String ARG_PREFIX = "--launcher-port=";

    private LauncherArgs() {}

    public static Integer resolvePort(String[] args) {
        String sysProp = System.getProperty(SYSTEM_PROPERTY_KEY);
        if (sysProp != null) {
            Integer parsed = tryParse(sysProp);
            if (parsed != null) return parsed;
        }

        if (args != null) {
            for (String arg : args) {
                if (arg.startsWith(ARG_PREFIX)) {
                    String value = arg.substring(ARG_PREFIX.length());
                    Integer parsed = tryParse(value);
                    if (parsed != null) return parsed;
                }
            }
        }

        return null;
    }

    private static Integer tryParse(String value) {
        try {
            return Integer.parseInt(value.trim());
        } catch (NumberFormatException e) {
            return null;
        }
    }
}