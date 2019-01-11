package edu.robustnet.xiao.mpwear;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

public class MainActivity extends Activity {

    @Override
//    @RequiresApi(api = Build.VERSION_CODES.O)
    protected void onCreate(Bundle savedInstanceState){
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_main);

        Intent intent = new Intent(this, PhoneProxy.class);
        startService(intent);

//        Intent intent = new Intent(this, WiFiEnabler.class);
//        startService(intent);

    }
}
