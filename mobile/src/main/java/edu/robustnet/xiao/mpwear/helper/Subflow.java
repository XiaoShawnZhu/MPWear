package edu.robustnet.xiao.mpwear.helper;

public class Subflow {

    private static final String TAG = "Subflow";

    public native String subflowFromJNI(String subflowIP, String rpIP);
    static {
        System.loadLibrary("proxy");
    }

}
