package fr.cerostudio.mixin;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Pseudo
@Mixin(targets = "net.minecraft.client.ClientBrandRetriever")
public class MixinClientBrand {

    @Inject(method = "getClientModName", at = @At("RETURN"), cancellable = true, require = 0)
    private static void cero$brand(CallbackInfoReturnable<String> cir) {
        cir.setReturnValue("CeroLoader/CeroClient");
    }
}