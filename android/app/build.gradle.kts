plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.digeweather.widget"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.digeweather.widget"
        minSdk = 26
        targetSdk = 35
        versionCode = (System.getenv("GITHUB_RUN_NUMBER") ?: "1").toInt()
        versionName = "1.0." + (System.getenv("GITHUB_RUN_NUMBER") ?: "0")
    }

    // Fixed signing key committed to the repo so every CI build can be
    // installed as an update over the previous one. Personal sideload use only.
    signingConfigs {
        create("sideload") {
            storeFile = file("sideload.keystore")
            storePassword = "digeweather"
            keyAlias = "digeweather"
            keyPassword = "digeweather"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("sideload")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation("androidx.work:work-runtime:2.9.1")
}
