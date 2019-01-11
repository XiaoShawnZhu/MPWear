package edu.robustnet.xiao.mpwear;

import android.app.IntentService;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkInfo;
import android.util.Log;

import java.io.File;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Collections;
import java.util.List;
import java.util.Scanner;

import edu.robustnet.xiao.mpwear.helper.BluetoothListener;
import edu.robustnet.xiao.mpwear.helper.CellEnabler;
import edu.robustnet.xiao.mpwear.helper.LocalConn;
import edu.robustnet.xiao.mpwear.helper.Pipe;
import edu.robustnet.xiao.mpwear.helper.Subflow;
import edu.robustnet.xiao.mpwear.helper.WiFiEnabler;
import edu.robustnet.xiao.mpwear.helper.WiFiListener;

public class PhoneProxy extends IntentService {

    private static final String TAG = "Secondary";
    private static final int BT_CHANNEL = 0;
    private static final int BLE_CHANNEL = 1;
    private static final int WIFI_CHANNEL = 2;
    private static final int LTE_CHANNEL = 3;
    private static BluetoothListener bluetoothListener = new BluetoothListener();
    private static WiFiListener wifiListener = new WiFiListener();
    private static Subflow subflow = new Subflow();
    private static Pipe pipe = new Pipe();
    private static WiFiEnabler wiFiEnabler = new WiFiEnabler();
    private static int pipeType;
    private static int subflowType;
    private static String subflowIP;
    private static String rpIP;
    private static LocalConn localConn = new LocalConn();
    private static String pathName = "/storage/emulated/0/mpwear/mpwear_config.txt";
    private static ConnectivityManager mConnectivityManager;


    @Override
//    @RequiresApi(api = Build.VERSION_CODES.O)
    protected void onHandleIntent(Intent arg0){
        mpwear();
    }

    public PhoneProxy(){
        super("MyIntentService");
    }

    public void mpwear(){

        mConnectivityManager = (ConnectivityManager) getSystemService(Context.CONNECTIVITY_SERVICE);

        processConfig(pathName);

        Log.d(TAG, "pipeType is " + pipeType + ", Subflow type/IP is " + subflowType + "/" + subflowIP + ", rpIP is " + rpIP);
        // local pipe listener
        Log.d(TAG, pipe.pipeSetupFromJNI(subflowIP, rpIP));
        // local pipe connector
        localPipeSetup(pipeType);
        // remote pipe listener
        remotePipeSetup(pipeType);
        // subflow connector
        if(subflowType==LTE_CHANNEL)
            useNetwork(mConnectivityManager.TYPE_MOBILE);
        if(subflowType==WIFI_CHANNEL)
            useNetwork(mConnectivityManager.TYPE_WIFI);
        subflowSetup();
    }

    private void remotePipeSetup(int pipeType){
        // set up a pipe with C++ according to the type specified
        switch(pipeType) {
            case BT_CHANNEL:
                bluetoothListener.connectPeer(); // start BT pipe listen
                Log.d(TAG,"connection peer complete");
//                bluetoothListener.connectPeerSide();
//                Log.d(TAG, "Side peer connection complete");
                break;
            case BLE_CHANNEL:
                break;
            case WIFI_CHANNEL:
                wifiListener.connectPeer(); // start WiFi pipe listen
                break;
        }
    }

    private void localPipeSetup(int pipeType){
        // set up a pipe with peer according to the type specified
        switch(pipeType) {
            case BT_CHANNEL:
                bluetoothListener.connectProxy(); // start C++/Java connect
                Log.d(TAG,"connection proxy complete");
//                bluetoothListener.connectProxySide();
//                Log.d(TAG, "Side proxy connection complete");
                break;
            case BLE_CHANNEL:
                break;
            case WIFI_CHANNEL:
                wifiListener.connectProxy(); // start C++/Java connect
                break;
        }
    }

    private void subflowSetup(){
        // set up secondary subflow
        while(true){
            if(pipeOk(pipeType)){

                Log.d(TAG, "pipe established, about to set up subflow");
                String r = subflow.subflowFromJNI(subflowIP, rpIP);
                if(r.equals("SubflowSetupSucc")){
                    Log.d(TAG, "Subflow setup success.");
                }
                else{
                    Log.d(TAG, r);
                    System.exit(0);
                }
                break;
            }
        }

        Log.d(TAG, localConn.connSetupFromJNI());
        // start proxy main
        proxyFromJNI();
    }

    private boolean pipeOk(int pipeType){
        switch (pipeType){
            case BT_CHANNEL:
                return bluetoothListener.getPipeStatus();
            case BLE_CHANNEL:
                //
            case WIFI_CHANNEL:
                return wifiListener.getPipeStatus();
                //
        }
        return false;
    }

    private void processConfig(String pathName){

        String pipeName = null, subflowName = null;
        File f = new File(pathName);
        try{
            Scanner sc = new Scanner(f);
            pipeName = sc.nextLine();
//            Log.d(TAG, pipeName);
            subflowName = sc.nextLine();
//            Log.d(TAG, subflowName);
            rpIP = sc.nextLine();
//            Log.d(TAG, rpIP);
            sc.close();
        }
        catch(Exception e){
            System.out.println(e);
        }

        switch(subflowName){
            case "wlan0":
                subflowType = WIFI_CHANNEL;
                startService(new Intent(this, WiFiEnabler.class));
                break;
            case "rmnet_data0":
            case "rmnet0":
                subflowType = LTE_CHANNEL;
                Log.d(TAG, "subflow is LTE.");
                startService(new Intent(this, CellEnabler.class));
                break;
            default:
                Log.d(TAG, "Subflow name doesn't match to any.");
                System.exit(0);
        }

        switch(pipeName){
            case "wlan0":
                pipeType = WIFI_CHANNEL;
                startService(new Intent(this, WiFiEnabler.class));
                break;
            case "lo":
                pipeType = BT_CHANNEL;
                break;
            default:
                Log.d(TAG, "Pipe name doesn't match to any.");
                System.exit(0);
        }

        try {
            // wait for intent to be fully started
            Thread.sleep(3000);
            List<NetworkInterface> interfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
            for (NetworkInterface intf : interfaces) {
//                Log.d(TAG, "Looped " + intf.getName());
                if (!intf.getName().equalsIgnoreCase(subflowName)) continue;
                List<InetAddress> addrs = Collections.list(intf.getInetAddresses());
                subflowIP = addrs.get(0).getHostAddress();
                Log.d(TAG, subflowIP);
            }
        }
        catch(Exception e){
            System.out.println(e);
        }

    }

    private void useNetwork(int type){

        if(type==-1){
            mConnectivityManager.bindProcessToNetwork(null);
        }
        else{
            for(Network network:mConnectivityManager.getAllNetworks()){
                NetworkInfo networkInfo = mConnectivityManager.getNetworkInfo(network);
                Log.d(TAG, networkInfo.getType() + networkInfo.getTypeName());
                if(networkInfo.getType()==type){
                    Log.d(TAG, networkInfo.getTypeName() + " prefer");
                    mConnectivityManager.bindProcessToNetwork(network);
                }
            }
        }

    }

    public native String proxyFromJNI();
    static {
        System.loadLibrary("proxy");
    }

}