plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Numéro de version fourni par la CI (numéro de run GitHub), sinon 1.
val ciVersionCode: Int = System.getenv("VERSION_CODE")?.toIntOrNull() ?: 1

android {
    namespace = "com.xona.assist"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.xona.assist"
        minSdk = 24
        targetSdk = 34
        versionCode = ciVersionCode
        versionName = "1.0.$ciVersionCode"
    }

    // Keystore fixe du projet : indispensable pour que chaque nouvelle APK
    // puisse mettre à jour la précédente sur le téléphone.
    signingConfigs {
        create("xona") {
            storeFile = file("xonaassist-release.jks")
            storePassword = "xonaassist-2026"
            keyAlias = "xonaassist"
            keyPassword = "xonaassist-2026"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("xona")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }

    // APK ARMv7 (32 bits) uniquement, pour les téléphones entrée de gamme type Redmi A3.
    splits {
        abi {
            isEnable = true
            reset()
            include("armeabi-v7a")
            isUniversalApk = false
        }
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.09.00")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.foundation:foundation")

    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.2")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.6")

    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
}
