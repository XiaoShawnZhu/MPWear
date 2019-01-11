package edu.robustnet.xiao.mpwear.helper;

public class Pipe {

    private static final String TAG = "Pipe";

    public native String pipeSetupFromJNI(String subflowIP, String rpIP);
    static {
        System.loadLibrary("proxy");
    }
}
