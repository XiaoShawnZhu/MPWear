package edu.robustnet.xiao.mpwear.helper;

import android.app.IntentService;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.util.Log;

public class CellEnabler extends IntentService {

    private static final String TAG = "OpenCell";
    //    public static final int WIFI_AVAILABLE = 32;
    ConnectivityManager mConnectivityManager;
    ConnectivityManager.NetworkCallback cellNetworkCallback = null;

    @Override
    protected void onHandleIntent(Intent arg0) {
        // TODO Auto-generated method stub
//        Log.i(TAG, "Intent Service started on OpenCell");
        mConnectivityManager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);
        // isNetworkAvailable(ConnectivityManager.TYPE_WIFI);
        enableCell();
        // isNetworkAvailable(ConnectivityManager.TYPE_WIFI);
    }

    private void enableCell() {
//        Log.d(TAG, "Request LTE");
        if (cellNetworkCallback != null) {
            return;
        }

        cellNetworkCallback =
                new ConnectivityManager.NetworkCallback() {
                    @Override
                    public void onAvailable(Network network) {
                        Log.d(TAG, "LTE Network available.");
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
                .addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR)
                //.addTransportType(NetworkCapabilities.TRANSPORT_BLUETOOTH)
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build();
        mConnectivityManager.requestNetwork(request, cellNetworkCallback);

    }

    public CellEnabler() {
        super("MyIntentService");
    }
}
