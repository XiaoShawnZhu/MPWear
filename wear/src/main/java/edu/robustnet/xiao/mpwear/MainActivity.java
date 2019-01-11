package edu.robustnet.xiao.mpwear;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.support.wearable.activity.WearableActivity;

public class MainActivity extends WearableActivity {

    private static final String TAG = "MainActivity";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Intent intent = new Intent(this, WearProxy.class);
        startService(intent);

    }


}
