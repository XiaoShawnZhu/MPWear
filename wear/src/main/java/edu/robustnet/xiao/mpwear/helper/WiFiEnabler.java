package edu.robustnet.xiao.mpwear.helper;

import android.app.IntentService;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.NetworkRequest;
import android.util.Log;

public class WiFiEnabler extends IntentService {

    private static final String TAG = "OpenWiFi";
//    public static final int WIFI_AVAILABLE = 32;
    ConnectivityManager mConnectivityManager;
    ConnectivityManager.NetworkCallback wifiNetworkCallback = null;

    @Override
    protected void onHandleIntent(Intent arg0) {
        // TODO Auto-generated method stub
        Log.i(TAG, "Intent Service started on OpenWiFi");
        mConnectivityManager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        // isNetworkAvailable(ConnectivityManager.TYPE_WIFI);
        enableWifi();
        // isNetworkAvailable(ConnectivityManager.TYPE_WIFI);
    }

    private void enableWifi() {
        Log.d(TAG, "Request WiFi");
        if (wifiNetworkCallback != null) {
            return;
        }
        wifiNetworkCallback =
                new ConnectivityManager.NetworkCallback() {
                    @Override
                    public void onAvailable(Network network) {
                        Log.d(TAG, "WiFi Network available.");
                        //eventBuffer.append(System.currentTimeMillis() + "\tWIFI_AVAILABLE\n");
                        //Handler handler = NetManager.getHandler();
//                        Message message = new Message();
//                        message.what = WIFI_AVAILABLE;
                        // handler.sendMessage(message);
//                        if (wifiConn == null) {
//                            btToWifiSwitch();
//                        }
                    }
                };
        NetworkRequest request = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                //.addTransportType(NetworkCapabilities.TRANSPORT_BLUETOOTH)
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build();
        mConnectivityManager.requestNetwork(request, wifiNetworkCallback);
    }

    private void disableWifi() {
        Log.d(TAG, "Release WiFi");
        if (wifiNetworkCallback != null) {
            mConnectivityManager.unregisterNetworkCallback(wifiNetworkCallback);
            wifiNetworkCallback = null;
        }
    }

    public boolean isNetworkAvailable(int netType){
        Log.d(TAG, "Checking " + netType + " availability");
        for(Network network:mConnectivityManager.getAllNetworks()){
            NetworkInfo networkInfo = mConnectivityManager.getNetworkInfo(network);
            Log.d(TAG, networkInfo.getType() + networkInfo.getTypeName());
            if(networkInfo.getType()==netType){
                Log.d(TAG, networkInfo.getTypeName() + " available");
                return true;
            }
        }
        Log.d(TAG, netType + " unavailable");
        return false;
    }

    public WiFiEnabler() {
        super("MyIntentService");
    }
}
