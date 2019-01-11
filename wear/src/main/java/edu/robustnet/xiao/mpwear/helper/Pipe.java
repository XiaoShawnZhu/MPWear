package edu.robustnet.xiao.mpwear.helper;

public class Pipe {

    private static final String TAG = "Pipe";

//    @Override
//    public void run() {
//        Log.d(TAG, connFromJNI());
//    }

    public native String pipeSetupFromJNI();
    static {
        System.loadLibrary("proxy");
    }
}
