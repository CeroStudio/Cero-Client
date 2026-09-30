package fr.cerostudio.remap;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.FieldVisitor;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Enumeration;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;
import java.util.jar.JarOutputStream;
import java.util.jar.Manifest;

public final class AccessWidener {

    private AccessWidener() {}

    public static byte[] widenClass(byte[] bytes) {
        ClassReader reader = new ClassReader(bytes);
        ClassWriter writer = new ClassWriter(0);
        reader.accept(new ClassVisitor(Opcodes.ASM9, writer) {
            @Override
            public void visit(int version, int access, String name, String signature, String superName, String[] interfaces) {
                super.visit(version, publicize(access), name, signature, superName, interfaces);
            }

            @Override
            public FieldVisitor visitField(int access, String name, String desc, String signature, Object value) {
                return super.visitField(publicize(access), name, desc, signature, value);
            }

            @Override
            public MethodVisitor visitMethod(int access, String name, String desc, String signature, String[] exceptions) {
                return super.visitMethod(publicize(access), name, desc, signature, exceptions);
            }
        }, 0);
        return writer.toByteArray();
    }

    public static byte[] widenClassSafe(byte[] bytes) {
        try {
            return widenClass(bytes);
        } catch (Exception e) {
            return bytes;
        }
    }

    public static void widenJar(Path jarPath) throws IOException {
        Path tmp = Files.createTempFile("cero-access", ".jar");
        try {
            try (JarFile src = new JarFile(jarPath.toFile());
                 JarOutputStream dst = new JarOutputStream(Files.newOutputStream(tmp), manifestOf(src))) {
                byte[] buf = new byte[8192];
                Enumeration<JarEntry> entries = src.entries();
                while (entries.hasMoreElements()) {
                    JarEntry entry = entries.nextElement();
                    if (entry.isDirectory() || isManifestEntry(entry.getName())) continue;
                    dst.putNextEntry(new JarEntry(entry.getName()));
                    try (InputStream is = src.getInputStream(entry)) {
                        if (isClassEntry(entry.getName())) {
                            ByteArrayOutputStream bos = new ByteArrayOutputStream();
                            int n;
                            while ((n = is.read(buf)) != -1) bos.write(buf, 0, n);
                            dst.write(widenClassSafe(bos.toByteArray()));
                        } else {
                            int n;
                            while ((n = is.read(buf)) != -1) dst.write(buf, 0, n);
                        }
                    }
                    dst.closeEntry();
                }
                dst.finish();
            }
            Files.move(tmp, jarPath, StandardCopyOption.REPLACE_EXISTING);
        } finally {
            Files.deleteIfExists(tmp);
        }
    }

    private static Manifest manifestOf(JarFile jar) throws IOException {
        Manifest manifest = jar.getManifest();
        return manifest != null ? manifest : new Manifest();
    }

    private static int publicize(int access) {
        return (access & ~(Opcodes.ACC_PRIVATE | Opcodes.ACC_PROTECTED)) | Opcodes.ACC_PUBLIC;
    }

    private static boolean isManifestEntry(String name) {
        return name.equalsIgnoreCase("META-INF/MANIFEST.MF");
    }

    private static boolean isClassEntry(String name) {
        return name.endsWith(".class") && !name.endsWith("module-info.class");
    }
}