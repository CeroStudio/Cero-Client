package fr.cerostudio;

import fr.cerostudio.api.CeroApi;
import fr.cerostudio.api.player.PlayerIdentity;

import java.io.File;
import java.lang.reflect.Method;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;

public class Main {

    public static void main(String[] args) throws Exception {
        CeroApi.captureLaunchArguments(args);
        String realMainClass = null;
        String mcVersion = "unknown";
        String username = null;
        String uuid = null;
        List<String> forwardedArgs = new ArrayList<>();

        for (int i = 0; i < args.length; i++) {
            if ("--realMainClass".equals(args[i]) && i + 1 < args.length) {
                realMainClass = args[i + 1];
                i++;
            } else if ("--ceroMcVersion".equals(args[i]) && i + 1 < args.length) {
                mcVersion = args[i + 1];
                i++;
            } else if ("--username".equals(args[i]) && i + 1 < args.length) {
                username = args[i + 1];
                forwardedArgs.add(args[i]);
                forwardedArgs.add(args[i + 1]);
                i++;
            } else if ("--uuid".equals(args[i]) && i + 1 < args.length) {
                uuid = args[i + 1];
                forwardedArgs.add(args[i]);
                forwardedArgs.add(args[i + 1]);
                i++;
            } else if (args[i].startsWith("--launcher-port=")) {
                // one token
            } else if ("--launcher-port".equals(args[i]) && i + 1 < args.length) {
                i++;
            } else if ("--ceroPort".equals(args[i]) && i + 1 < args.length) {
                i++;
            } else {
                forwardedArgs.add(args[i]);
            }
        }

        if (realMainClass == null) {
            System.err.println("[CeroClient] --realMainClass manquant, abandon.");
            System.exit(1);
            return;
        }

        System.out.println("[CeroClient] Démarrage — Version = " + mcVersion + " | mainClass = " + realMainClass);

        PlayerIdentity identity = PlayerIdentity.resolve(username, uuid);

        URL[] classpathUrls = extractClasspathUrls();

        RemappingClassLoader remapper = new RemappingClassLoader(
                classpathUrls,
                Main.class.getClassLoader(),
                mcVersion,
                identity
        );

        Thread.currentThread().setContextClassLoader(remapper);

        Class<?> mainClass = Class.forName(realMainClass, true, remapper);
        Method mainMethod = mainClass.getMethod("main", String[].class);
        mainMethod.setAccessible(true);

        mainMethod.invoke(null, (Object) forwardedArgs.toArray(new String[0]));
    }

    private static URL[] extractClasspathUrls() {
        String cp = System.getProperty("java.class.path", "");
        String sep = System.getProperty("path.separator", ":");
        String[] parts = cp.split(Pattern.quote(sep));
        List<URL> urls = new ArrayList<>();
        for (String p : parts) {
            try {
                urls.add(new File(p).toURI().toURL());
            } catch (Exception e) {
                System.err.println("[CeroClient] Entrée classpath ignorée: " + p);
            }
        }
        return urls.toArray(new URL[0]);
    }
}