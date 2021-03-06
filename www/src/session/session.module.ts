import { NgModule }             from '@angular/core';
import { CommonModule }         from '@angular/common';
import { FormsModule }          from '@angular/forms';

import { SessionGuard }         from './session.guard';
import { SessionRoutingModule } from './routing.module';
import { LoginComponent }       from './login.component';
import { ErrorComponent }       from './error.component';
import { ErrorResolver }        from './error.resolver';

@NgModule( {
	imports: [
		CommonModule,
		FormsModule,
		SessionRoutingModule
	],
	declarations: [
		LoginComponent,
		ErrorComponent
	],
	exports: [
	],
	// SessionService is not added as a provider here. Instead it is a provider for the parent app
	// module so all other child modules use the same instance of SessionService.
	providers: [
		SessionGuard,
		ErrorResolver
	]
} )

export class SessionModule {
}
