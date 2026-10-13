package me.razqqm.smartkey

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    PlaceholderScreen()
                }
            }
        }
    }
}

@Composable
fun PlaceholderScreen() {
    Column(modifier = Modifier.padding(24.dp)) {
        Text(
            text = stringResource(R.string.placeholder_title),
            style = MaterialTheme.typography.headlineMedium
        )
        Spacer(Modifier.height(12.dp))
        Text(text = stringResource(R.string.placeholder_body))
    }
}

@Preview(showBackground = true)
@Composable
fun PlaceholderPreview() {
    MaterialTheme { PlaceholderScreen() }
}
