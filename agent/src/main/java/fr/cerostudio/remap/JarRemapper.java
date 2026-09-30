package fr.cerostudio.remap;

import net.fabricmc.tinyremapper.TinyRemapper;
import net.fabricmc.tinyremapper.TinyUtils;
import net.fabricmc.tinyremapper.OutputConsumerPath;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.*;
import java.util.jar.Manifest;

public class JarRemapper {
    public static void main(String[] args) {
        if (args.length < 3) {
            System.err.println("[CeroRemapper] Usage: <input.jar> <output.jar> <mappings.tiny>");
            System.exit(1);
        }

        Path inputJar = Paths.get(args[0]);
        Path outputJar = Paths.get(args[1]);
        Path mappingFile = Paths.get(args[2]);

        System.out.println("[CeroRemapper] Début du remapping de " + inputJar.getFileName() + "...");

        try {
            Files.deleteIfExists(outputJar);

            TinyRemapper remapper = TinyRemapper.newRemapper()
                    .withMappings(TinyUtils.createTinyMappingProvider(mappingFile, "official", "named"))
                    .build();

            try (OutputConsumerPath outputConsumer = new OutputConsumerPath.Builder(outputJar).build()) {
                outputConsumer.addNonClassFiles(inputJar);
                remapper.readInputs(inputJar);
                remapper.apply(outputConsumer);
            } finally {
                remapper.finish();
            }

            stripSignatureAndManifest(outputJar);
            AccessWidener.widenJar(outputJar);

            System.out.println("[CeroRemapper] Remapping terminé avec succès !");
        } catch (IOException e) {
            System.err.println("[CeroRemapper] Erreur durant le remapping : " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
    }

    private static void stripSignatureAndManifest(Path jarPath) throws IOException {
        System.out.println("[CeroRemapper] Nettoyage des signatures Mojang...");
        try (FileSystem fs = FileSystems.newFileSystem(jarPath, (ClassLoader) null)) {
            Path metaInf = fs.getPath("META-INF");
            if (!Files.exists(metaInf)) return;

            try (DirectoryStream<Path> stream = Files.newDirectoryStream(metaInf)) {
                for (Path entry : stream) {
                    String name = entry.getFileName().toString().toUpperCase();
                    if (name.endsWith(".SF") || name.endsWith(".RSA") || name.endsWith(".DSA") || name.endsWith(".EC")) {
                        Files.delete(entry);
                    }
                }
            }

            Path manifestPath = metaInf.resolve("MANIFEST.MF");
            if (Files.exists(manifestPath)) {
                try (InputStream is = Files.newInputStream(manifestPath)) {
                    Manifest mf = new Manifest(is);
                    mf.getEntries().clear();

                    try (OutputStream os = Files.newOutputStream(manifestPath)) {
                        mf.write(os);
                    }
                }
            }
        }
        System.out.println("[CeroRemapper] Signatures nettoyées avec succès.");
    }
}