package fr.cerostudio.mixin;

import fr.cerostudio.api.CeroApi;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyArg;
import org.spongepowered.asm.mixin.injection.ModifyVariable;

public final class MixinWindowTitle {

    private MixinWindowTitle() {}

    @Mixin(targets = "net.minecraft.client.Minecraft")
    public static class Legacy {

        @ModifyArg(
            method = { "func_71384_a", "func_175609_am" },
            at = @At(
                value = "INVOKE",
                target = "Lorg/lwjgl/opengl/Display;setTitle(Ljava/lang/String;)V"
            ),
            index = 0
        )
        private String cero$resolveTitle(String originalTitle) {
            return CeroApi.window().resolve(originalTitle);
        }
    }

    @Mixin(targets = "com.mojang.blaze3d.platform.Window")
    public static class Modern {

        @ModifyArg(
                method = "<init>",
                at = @At(
                        value = "INVOKE",
                        target = "Lorg/lwjgl/glfw/GLFW;glfwCreateWindow(IILjava/lang/CharSequence;JJ)J"
                ),
                index = 2
        )
        private CharSequence cero$resolveCreateWindowTitle(CharSequence originalTitle) {
            return CeroApi.window().resolve(originalTitle.toString());
        }

        @ModifyArg(
                method = "setTitle",
                at = @At(
                        value = "INVOKE",
                        target = "Lorg/lwjgl/glfw/GLFW;glfwSetWindowTitle(JLjava/lang/CharSequence;)V"
                ),
                index = 1
        )
        private CharSequence cero$resolveSetTitle(CharSequence originalTitle) {
            return CeroApi.window().resolve(originalTitle.toString());
        }
    }

    @Mixin(targets = "com.mojang.blaze3d.platform.Window")
    public static class Sdl {

        @ModifyVariable(method = "createWindow", at = @At("HEAD"), argsOnly = true)
        private String cero$resolveCreateTitle(String title) {
            return CeroApi.window().resolve(title);
        }

        @ModifyVariable(method = "setTitle", at = @At("HEAD"), argsOnly = true)
        private String cero$resolveSetTitle(String title) {
            return CeroApi.window().resolve(title);
        }
    }
}
