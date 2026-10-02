package fr.cerostudio.mixin;

import fr.cerostudio.api.CeroApi;
import fr.cerostudio.api.event.client.ServerConnectEvent;
import fr.cerostudio.api.event.client.ServerDisconnectEvent;
import fr.cerostudio.api.event.client.WorldJoinEvent;
import fr.cerostudio.api.event.client.WorldLeaveEvent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.lang.reflect.Method;

@Mixin(targets = "net.minecraft.client.multiplayer.ClientPacketListener")
public final class MixinWorldLifecycle {

    private boolean cero$wasMultiplayer;

    @Inject(method = "<init>", at = @At("RETURN"))
    private void cero$onConnect(CallbackInfo ci) {
        long now = System.currentTimeMillis();
        String address = cero$readServerAddress();
        cero$wasMultiplayer = address != null;
        if (cero$wasMultiplayer) {
            CeroApi.events().post(new ServerConnectEvent(address, now));
        } else {
            CeroApi.events().post(new WorldJoinEvent(now));
        }
    }

    @Inject(method = "onDisconnect", at = @At("HEAD"))
    private void cero$onDisconnect(CallbackInfo ci) {
        long now = System.currentTimeMillis();
        if (cero$wasMultiplayer) {
            CeroApi.events().post(new ServerDisconnectEvent(now));
        } else {
            CeroApi.events().post(new WorldLeaveEvent(now));
        }
    }

    private static String cero$readServerAddress() {
        try {
            Class<?> mcClass = Class.forName("net.minecraft.client.Minecraft");
            Object instance = mcClass.getMethod("getInstance").invoke(null);
            Method getServerData = mcClass.getMethod("getCurrentServerData");
            Object serverData = getServerData.invoke(instance);
            if (serverData == null) {
                return null;
            }
            Object ip = serverData.getClass().getField("ip").get(serverData);
            return ip != null ? ip.toString() : null;
        } catch (ReflectiveOperationException | RuntimeException e) {
            System.err.println("[MixinWorldLifecycle] Lecture de Minecraft#getCurrentServerData impossible "
                    + "par réflexion (API renommée sur cette version ?) - je considère que c'est une session "
                    + "solo : " + e);
            return null;
        }
    }
}