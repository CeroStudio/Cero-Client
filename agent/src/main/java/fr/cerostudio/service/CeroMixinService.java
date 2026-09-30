package fr.cerostudio.service;

import fr.cerostudio.RemappingClassLoader;
import org.objectweb.asm.tree.ClassNode;
import org.spongepowered.asm.launch.platform.container.IContainerHandle;
import org.spongepowered.asm.logging.ILogger;
import org.spongepowered.asm.mixin.MixinEnvironment;
import org.spongepowered.asm.service.*;
import org.spongepowered.asm.util.ReEntranceLock;

import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.util.Collection;
import java.util.Collections;

public class CeroMixinService implements IMixinService, IClassProvider, IClassBytecodeProvider,
        ITransformerProvider, IClassTracker, IMixinAuditTrail {

    private final ReEntranceLock lock = new ReEntranceLock(1);

    public CeroMixinService() {}

    @Override public String getName() { return "Cero ClassLoader Service"; }
    @Override public boolean isValid() { return true; }
    @Override public void prepare() {}
    @Override public MixinEnvironment.Phase getInitialPhase() { return MixinEnvironment.Phase.DEFAULT; }

    private org.spongepowered.asm.mixin.transformer.IMixinTransformerFactory transformerFactory;
    private org.spongepowered.asm.mixin.transformer.IMixinTransformer transformer;

    @Override
    public void offer(IMixinInternal internal) {
        if (internal instanceof org.spongepowered.asm.mixin.transformer.IMixinTransformerFactory) {
            this.transformerFactory = (org.spongepowered.asm.mixin.transformer.IMixinTransformerFactory) internal;
        }
    }

    public org.spongepowered.asm.mixin.transformer.IMixinTransformer getTransformer() {
        if (transformer == null && transformerFactory != null) {
            try {
                transformer = transformerFactory.createTransformer();
            } catch (org.spongepowered.asm.launch.MixinInitialisationError e) {
                e.printStackTrace();
            }
        }
        return transformer;
    }

    @Override public void init() {}
    @Override public void beginPhase() {}
    @Override public void checkEnv(Object bootSource) {}
    @Override public ReEntranceLock getReEntranceLock() { return lock; }
    @Override public IClassProvider getClassProvider() { return this; }
    @Override public IClassBytecodeProvider getBytecodeProvider() { return this; }
    @Override public ITransformerProvider getTransformerProvider() { return this; }
    @Override public IClassTracker getClassTracker() { return this; }
    @Override public IMixinAuditTrail getAuditTrail() { return this; }
    @Override public IFeatureValidator getFeatureValidator() { return IFeatureValidator.ALLOW_ALL; }
    @Override public IAdviceProvider getAdviceProvider() { return IAdviceProvider.GENERIC; }
    @Override public Collection<String> getPlatformAgents() { return Collections.emptyList(); }
    private final IContainerHandle primaryContainer = new org.spongepowered.asm.launch.platform.container.ContainerHandleVirtual("CeroClient");

    @Override public IContainerHandle getPrimaryContainer() { return primaryContainer; }
    @Override public Collection<IContainerHandle> getMixinContainers() { return Collections.emptyList(); }
    @Override public String getSideName() { return "CLIENT"; }
    @Override public MixinEnvironment.CompatibilityLevel getMinCompatibilityLevel() { return MixinEnvironment.CompatibilityLevel.JAVA_8; }
    @Override public MixinEnvironment.CompatibilityLevel getMaxCompatibilityLevel() { return MixinEnvironment.CompatibilityLevel.JAVA_25; }
    @Override
    public ILogger getLogger(String name) {
        return new ILogger() {
            @Override public String getId() { return "cero"; }
            @Override public String getType() { return "CeroLogger"; }
            @Override public void catching(org.spongepowered.asm.logging.Level level, Throwable t) { t.printStackTrace(); }
            @Override public void catching(Throwable t) { t.printStackTrace(); }
            @Override public void debug(String message, Object... params) { System.out.printf("[DEBUG] [" + name + "] " + message + "%n", params); }
            @Override public void debug(String message, Throwable t) { System.out.println("[DEBUG] [" + name + "] " + message); t.printStackTrace(); }
            @Override public void error(String message, Object... params) { System.err.printf("[ERROR] [" + name + "] " + message + "%n", params); }
            @Override public void error(String message, Throwable t) { System.err.println("[ERROR] [" + name + "] " + message); t.printStackTrace(); }
            @Override public void fatal(String message, Object... params) { System.err.printf("[FATAL] [" + name + "] " + message + "%n", params); }
            @Override public void fatal(String message, Throwable t) { System.err.println("[FATAL] [" + name + "] " + message); t.printStackTrace(); }
            @Override public void info(String message, Object... params) { System.out.printf("[INFO] [" + name + "] " + message + "%n", params); }
            @Override public void info(String message, Throwable t) { System.out.println("[INFO] [" + name + "] " + message); t.printStackTrace(); }
            @Override public void log(org.spongepowered.asm.logging.Level level, String message, Object... params) { System.out.printf("[" + level + "] [" + name + "] " + message + "%n", params); }
            @Override public void log(org.spongepowered.asm.logging.Level level, String message, Throwable t) { System.out.println("[" + level + "] [" + name + "] " + message); t.printStackTrace(); }
            @Override public <T extends Throwable> T throwing(T t) { return t; }
            @Override public void trace(String message, Object... params) { System.out.printf("[TRACE] [" + name + "] " + message + "%n", params); }
            @Override public void trace(String message, Throwable t) { System.out.println("[TRACE] [" + name + "] " + message); t.printStackTrace(); }
            @Override public void warn(String message, Object... params) { System.out.printf("[WARN] [" + name + "] " + message + "%n", params); }
            @Override public void warn(String message, Throwable t) { System.out.println("[WARN] [" + name + "] " + message); t.printStackTrace(); }
        };
    }

    @Override
    public InputStream getResourceAsStream(String name) {
        return getClassLoader().getResourceAsStream(name);
    }

    private ClassLoader getClassLoader() {
        ClassLoader cl = Thread.currentThread().getContextClassLoader();
        System.out.println("[DEBUG] context CL = " + cl.getClass().getName());
        return cl;
    }

    @Override
    public URL[] getClassPath() {
        return new URL[0];
    }

    @Override
    public Class<?> findClass(String name) throws ClassNotFoundException {
        return Class.forName(name, true, getClassLoader());
    }

    @Override
    public Class<?> findClass(String name, boolean initialize) throws ClassNotFoundException {
        return Class.forName(name, initialize, getClassLoader());
    }

    @Override
    public Class<?> findAgentClass(String name, boolean initialize) throws ClassNotFoundException {
        return Class.forName(name, initialize, getClassLoader());
    }

    @Override
    public ClassNode getClassNode(String name) throws ClassNotFoundException, IOException {
        ClassLoader cl = getClassLoader();
        if (cl instanceof RemappingClassLoader) {
            byte[] bytes = ((RemappingClassLoader) cl).readResourceBytes(name.replace('.', '/') + ".class");
            if (bytes == null) throw new ClassNotFoundException(name);
            ClassNode node = new ClassNode();
            new org.objectweb.asm.ClassReader(bytes).accept(node, 0);
            return node;
        }
        throw new ClassNotFoundException("CeroMixinService cannot load outside RemappingClassLoader: " + name);
    }

    @Override
    public ClassNode getClassNode(String name, boolean runTransformers) throws ClassNotFoundException, IOException {
        return getClassNode(name);
    }

    @Override
    public ClassNode getClassNode(String name, boolean runTransformers, int flags) throws ClassNotFoundException, IOException {
        ClassLoader cl = getClassLoader();
        if (cl instanceof RemappingClassLoader) {
            byte[] bytes = ((RemappingClassLoader) cl).readResourceBytes(name.replace('.', '/') + ".class");
            if (bytes == null) throw new ClassNotFoundException(name);
            ClassNode node = new ClassNode();
            new org.objectweb.asm.ClassReader(bytes).accept(node, flags);
            return node;
        }
        throw new ClassNotFoundException("CeroMixinService cannot load outside RemappingClassLoader: " + name);
    }

    @Override
    public Collection<ITransformer> getTransformers() {
        return Collections.emptyList();
    }

    @Override
    public Collection<ITransformer> getDelegatedTransformers() {
        return Collections.emptyList();
    }

    @Override
    public void addTransformerExclusion(String name) {
    }

    @Override
    public void registerInvalidClass(String className) {
    }

    @Override
    public boolean isClassLoaded(String className) {
        return false;
    }

    @Override
    public String getClassRestrictions(String className) {
        return "";
    }

    @Override
    public void onApply(String className, String mixinName) {  }

    @Override
    public void onPostProcess(String className) {  }

    @Override
    public void onGenerate(String className, String proxyName) {  }
}