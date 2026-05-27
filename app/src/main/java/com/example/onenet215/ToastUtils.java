package com.example.onenet215;

import android.content.Context;
import android.widget.Toast;

public class ToastUtils {
    private static Toast currentToast;

    public static void show(Context context, String message) {
        if (currentToast != null) {
            currentToast.cancel();
        }
        currentToast = Toast.makeText(context.getApplicationContext(), message, Toast.LENGTH_SHORT);
        currentToast.show();
    }

    public static void showLong(Context context, String message) {
        if (currentToast != null) {
            currentToast.cancel();
        }
        currentToast = Toast.makeText(context.getApplicationContext(), message, Toast.LENGTH_LONG);
        currentToast.show();
    }
}