package edu.robustnet.xiao.mpwear.helper;

import android.util.Log;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

public class WiFiListener {

    private static final String TAG = "WiFiListener";

    private boolean isPipeEstablished = false;
    String proxyIP = "127.0.0.1";
    int proxyPort = 1303;
    private static InputStream wifiInputStream;
    private static OutputStream wifiOutputStream;
    private static InputStream localInputStream;
    private static OutputStream localOutputStream;
    private static int segmentSize = 1000;
    private static int recvBufSize = 8192;
    private static int fileSize = 10000000;
    private static byte[] sendData = new byte[fileSize];
    static BlockingQueue<PipeMsg> toLocalQueue = new ArrayBlockingQueue<PipeMsg>(1000000);
    static BlockingQueue<PipeMsg> toBTQueue = new ArrayBlockingQueue<PipeMsg>(1000000);

    public void connectPeer() {
        try {
            ServerSocket welcomeSocket = new ServerSocket(5000);
            Log.d(TAG, "WiFi pipe listening.");
            Socket connectionSocket = welcomeSocket.accept();
            Log.d(TAG, "WiFi pipe connected.");
            isPipeEstablished = true;
            wifiInputStream = connectionSocket.getInputStream();
            wifiOutputStream = connectionSocket.getOutputStream();
            BtRead btRead = new BtRead();
            BtWrite btWrite = new BtWrite();
            btRead.start();
            btWrite.start();
        } catch (Exception e) {
            System.out.println("EXCEPTION!");
        }
    }

    public boolean getPipeStatus() {
        return isPipeEstablished;
    }

    public void connectProxy() {

        try {
            Socket socket = new Socket();
            socket.connect(new InetSocketAddress(proxyIP, proxyPort));
            localInputStream = socket.getInputStream();
            localOutputStream = socket.getOutputStream();
            LocalRead localRead = new LocalRead();
            localRead.start();
            LocalWrite localWrite = new LocalWrite();
            localWrite.start();

        } catch (Exception e) {
            System.out.println(e);
        }

    }

    // thread to handle data Rx over BT
    public class BtRead extends Thread {

        public void run(){
            recvDataFromBT();
        }

    }

    // thread to handle data Tx over BT
    public class BtWrite extends Thread {

        public void run(){
            sendDataToBT();
        }

    }

    // send data over BT socket
    public void sendDataToBT() {
        int dataSent;
        int sum = 0;
        try{
            while(true){
                PipeMsg pipeMsg = toBTQueue.take();
                byte[] buf = pipeMsg.getData();
                dataSent = buf.length;
                sum += dataSent;
//                Log.d(TAG,"DATA SENT IS: " + dataSent + " the sum is: " + sum);
                wifiOutputStream.write(buf, 0, buf.length);
            }
        }
        catch(Exception e){
            System.out.println(e);
        }
    }

    // recv data from BT socket
    public void recvDataFromBT(){
        byte[] buf = new byte[8192];
        int bytesRead;
        try{
            while(true){
                bytesRead = wifiInputStream.read(buf);
                String msg = new String(buf).substring(0, bytesRead);
                // add received msgs to msgQueue
                PipeMsg pipeMsg = new PipeMsg();
                byte[] realbuf = new byte[bytesRead];
                System.arraycopy(buf,0,realbuf,0,bytesRead);
                pipeMsg.setData(realbuf);
                toLocalQueue.add(pipeMsg);
            }
        }
        catch(Exception e){
            System.out.println(e);
        }
    }

    // Forwarding data received from BT to local
    private class LocalWrite extends Thread {

        @Override
        public void run(){
            sendDataToLocal();
        }
    }

    // Reading data from local and add it to msgQueue
    private class LocalRead extends Thread {

        @Override
        public void run(){
            recvDataFromLocal();
        }
    }

    // send data over local socket
    public void sendDataToLocal() {
        try{
            while(true){
                // take msg from msgQueue and write it to C++ proxy
                PipeMsg toLocalMsg;
                toLocalMsg = toLocalQueue.take();
                byte [] data = toLocalMsg.getData();
                localOutputStream.write(data, 0, data.length);
            }
        }
        catch(Exception e){
            System.out.println(e);
        }
    }

    // recv data from local socket
    public void recvDataFromLocal(){
        byte[] buf = new byte[8192];
        int bytesRead;
        try{
            while(true)
            {
                // read data from local socket and add it to the msgQueue
                bytesRead = localInputStream.read(buf);
//                Log.d(TAG, "successfully read " + bytesRead);
                PipeMsg fromLocalMsg = new PipeMsg();
                byte[] realbuf = new byte[bytesRead];
                System.arraycopy(buf,0,realbuf,0,bytesRead);
                fromLocalMsg.setData(realbuf);
                toBTQueue.add(fromLocalMsg);
            }
        }
        catch(Exception e){
            System.out.println(e);
        }
    }


}
