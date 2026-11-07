package com.larl.loader;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.TextView;

/** Entry point kept separate from the legacy compatibility namespace. */
public final class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView status = new TextView(this);
        status.setGravity(Gravity.CENTER);
        status.setText("Starting LARL 2.217 diagnostic host…");
        status.setTextSize(18);
        setContentView(status);
        startActivity(new Intent(this, com.roblox.client.ActivityGlView.class));
    }
}
